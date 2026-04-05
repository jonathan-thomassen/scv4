/*
 * Unit tests for SimonCollision.c
 *
 * Includes SimonCollision.c directly to gain access to static functions.
 * Provides mock implementations for TLN_GetLayerTile and FileOpen.
 *
 * Build (from build/ directory):
 *   cmake --build . --target test_collision
 *
 * Run:
 *   ./sc4/test_collision
 */

#ifndef LIB_EXPORTS
#define LIB_EXPORTS /* TLNAPI → dllexport / visibility, not dllimport */
#endif

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Pre-include headers so their guards prevent re-inclusion from SimonCollision.c */
#include "LoadFile.h"
#include "SimonCollision.h"
#include "Tilengine.h"

/* ========================================================================
 * Mock: tile grid
 * ========================================================================
 * 8×8-pixel tiles.  mock_grid[ty][tx] == true ⇒ solid.
 * TLN_GetLayerTile computes xoffset/yoffset from the pixel coordinate.
 * ====================================================================== */

#define MOCK_GRID_W 64
#define MOCK_GRID_H 64
static bool mock_grid[MOCK_GRID_H][MOCK_GRID_W];

static void mock_grid_clear(void) { memset(mock_grid, 0, sizeof(mock_grid)); }

static void mock_grid_set(int tile_x, int tile_y, bool solid) {
  if (tile_x >= 0 && tile_x < MOCK_GRID_W && tile_y >= 0 && tile_y < MOCK_GRID_H)
    mock_grid[tile_y][tile_x] = solid;
}

/* Fill a rectangular region of tiles [tx1,ty1) .. [tx2,ty2) */
static void mock_grid_fill(int tx1, int ty1, int tx2, int ty2) {
  for (int ty = ty1; ty < ty2; ty++)
    for (int tx = tx1; tx < tx2; tx++)
      mock_grid_set(tx, ty, true);
}

/* ---- Mock implementation of TLN_GetLayerTile ---- */
TLNAPI bool TLN_GetLayerTile(int nlayer, int x, int y, TLN_TileInfo *info) {
  (void)nlayer;
  memset(info, 0, sizeof(*info));
  if (x < 0 || y < 0) {
    info->empty = true;
    return true;
  }
  int tx = x / 8;
  int ty = y / 8;
  info->xoffset = x % 8;
  info->yoffset = y % 8;
  if (tx >= MOCK_GRID_W || ty >= MOCK_GRID_H) {
    info->empty = true;
  } else {
    info->empty = !mock_grid[ty][tx];
    if (!info->empty) {
      info->index = 1;
    }
  }
  return true;
}

/* ========================================================================
 * Mock: FileOpen
 * ======================================================================== */
static FILE *mock_file_ptr = NULL;

FILE *FileOpen(const char *filename) {
  (void)filename;
  return mock_file_ptr;
}

/* ========================================================================
 * Include source under test — gives access to all static functions
 * ======================================================================== */
#include "SimonCollision.c"

/* ========================================================================
 * Minimal test harness
 * ======================================================================== */
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)

#define RUN_TEST(name)                                                                             \
  do {                                                                                             \
    tests_run++;                                                                                   \
    printf("  %-55s ", #name);                                                                     \
    fflush(stdout);                                                                                \
    name();                                                                                        \
    tests_passed++;                                                                                \
    printf("PASS\n");                                                                              \
  } while (0)

/* ASSERT_EQ jumps out of the current test on failure via return.
 * The RUN_TEST macro skips the tests_passed++ increment, leaving the
 * count off by one — we track failures separately. */
#define ASSERT_EQ(a, b)                                                                            \
  do {                                                                                             \
    int _a = (a), _b = (b);                                                                        \
    if (_a != _b) {                                                                                \
      printf("FAIL\n    %s:%d: %s == %d, expected %d\n", __FILE__, __LINE__, #a, _a, _b);          \
      tests_failed++;                                                                              \
      return;                                                                                      \
    }                                                                                              \
  } while (0)

#define ASSERT_TRUE(cond)                                                                          \
  do {                                                                                             \
    if (!(cond)) {                                                                                 \
      printf("FAIL\n    %s:%d: %s is false\n", __FILE__, __LINE__, #cond);                         \
      tests_failed++;                                                                              \
      return;                                                                                      \
    }                                                                                              \
  } while (0)

/* ========================================================================
 * Helper: position constants
 *
 * All tests use world_x = 0, sprite_x = 32 so that:
 *   left  probe X = 0 + 32 + 8  = 40   (tile 5, offset 0)
 *   right probe X = 0 + 32 + 24 = 56   (tile 7, offset 0)
 *
 * sprite_y = 64 so that:
 *   top    probe Y = 64                 (tile 8, offset 0)
 *   bottom probe Y = 64 + 45 = 109     (tile 13, offset 5)
 * ======================================================================== */
#define T_WORLD_X 0
#define T_SPRITE_X 32
#define T_SPRITE_Y 64

/* Tile indices for the probes (pixel / 8) */
#define T_LEFT_TX 5    /* 40 / 8 */
#define T_RIGHT_TX 7   /* 56 / 8 */
#define T_TOP_TY 8     /* 64 / 8 */
#define T_BOTTOM_TY 13 /* 109 / 8 */

/* ========================================================================
 * Tests: resolve_collision — single-axis movement
 * ======================================================================== */

TEST(test_zero_displacement) {
  mock_grid_clear();
  int dx = 0, dy = 0;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &dx, &dy);
  ASSERT_EQ(dx, 0);
  ASSERT_EQ(dy, 0);
}

TEST(test_move_right_empty) {
  mock_grid_clear();
  int dx = 4, dy = 0;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &dx, &dy);
  ASSERT_EQ(dx, 4);
  ASSERT_EQ(dy, 0);
}

TEST(test_move_right_solid) {
  mock_grid_clear();
  /* Place a solid column of tiles at the right probe X + some offset.
   * Right probe is at pixel 56.  With dx = 4 the query hits pixel 60.
   * Tile at 60/8 = 7, offset = 60%8 = 4.
   * Tile 7 starts at pixel 56.  Make tile column 7 solid for all probe rows. */
  for (int ty = T_TOP_TY; ty <= T_BOTTOM_TY + 1; ty++)
    mock_grid_set(T_RIGHT_TX, ty, true);

  int dx = 4, dy = 0;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &dx, &dy);
  /* dx should be clamped: dx -= xoffset = 4 − 4 = 0 */
  ASSERT_EQ(dx, 0);
  ASSERT_EQ(dy, 0);
}

TEST(test_move_right_partial_clamp) {
  mock_grid_clear();
  /* Right probe at pixel 56.  dx=6 → query pixel 62, tile 7 offset 6.
   * Tile 7 solid → dx -= 6 = 0.  But if tile 8 is solid instead, dx=6
   * hits pixel 62 (still tile 7, empty).  Let's put tile 8 (pixel 64):
   * dx=8 → pixel 64, tile 8, offset 0 → dx -= 0 = 8. Still passes.
   *
   * Better: tile 7 solid, dx=2 → pixel 58, offset 2 → dx -= 2 = 0 */
  for (int ty = T_TOP_TY; ty <= T_BOTTOM_TY + 1; ty++)
    mock_grid_set(T_RIGHT_TX, ty, true);

  int dx = 2, dy = 0;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &dx, &dy);
  ASSERT_EQ(dx, 0);
  ASSERT_EQ(dy, 0);
}

TEST(test_move_left_empty) {
  mock_grid_clear();
  int dx = -4, dy = 0;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &dx, &dy);
  ASSERT_EQ(dx, -4);
  ASSERT_EQ(dy, 0);
}

TEST(test_move_left_solid) {
  mock_grid_clear();
  /* Left probe at pixel 40 (tile 5, offset 0).
   * dx = -4 → query pixel 36, tile 4, offset 4.
   * Make tile 4 solid.
   * Correction: dx += TILE_SIZE - xoffset = -4 + (8 - 4) = 0 */
  for (int ty = T_TOP_TY; ty <= T_BOTTOM_TY + 1; ty++)
    mock_grid_set(T_LEFT_TX - 1, ty, true);

  int dx = -4, dy = 0;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &dx, &dy);
  ASSERT_EQ(dx, 0);
  ASSERT_EQ(dy, 0);
}

TEST(test_move_up_empty) {
  mock_grid_clear();
  int dx = 0, dy = -4;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &dx, &dy);
  ASSERT_EQ(dx, 0);
  ASSERT_EQ(dy, -4);
}

TEST(test_move_up_solid) {
  mock_grid_clear();
  /* Top probe at pixel Y=64 (tile 8, offset 0).
   * dy = -4 → query Y=60, tile 7, offset 4.
   * Make tile row 7 solid at both probe X columns.
   * Correction: dy += TILE_SIZE - yoffset = -4 + (8 - 4) = 0 */
  mock_grid_set(T_LEFT_TX, T_TOP_TY - 1, true);
  mock_grid_set(T_LEFT_TX + 1, T_TOP_TY - 1, true);

  int dx = 0, dy = -4;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &dx, &dy);
  ASSERT_EQ(dx, 0);
  ASSERT_EQ(dy, 0);
}

TEST(test_move_down_empty) {
  mock_grid_clear();
  int dx = 0, dy = 4;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &dx, &dy);
  ASSERT_EQ(dx, 0);
  ASSERT_EQ(dy, 4);
}

TEST(test_move_down_solid) {
  mock_grid_clear();
  /* Bottom probe at pixel Y=109 (tile 13, offset 5).
   * dy = 3 → query Y=112, tile 14, offset 0.
   * Make tile row 14 solid at both X columns.
   * Correction: dy -= yoffset = 3 − 0 = 3.  That's unchanged because offset is 0.
   *
   * Try: dy = 4 → Y=113, tile 14, offset 1 → dy -= 1 = 3.
   * Or: make tile 13 solid (bottom probe is already at Y=109 in tile 13).
   *  Then dy=1 → Y=110, tile 13, offset 6 → dy -= 6… that's -5, wrong.
   *
   * Re-read the code:  move_down_probes checks
   *   SIMON_COL_BOTTOM_Y(sprite_y) + *dy  =  109 + dy.
   * If dy=3 → Y=112 → tile=14, yoffset=0.  Tile 14 solid:
   *   *dy -= v_tile.yoffset → dy -= 0 = 3.  No change!
   *
   * The semantics: yoffset=0 means the probe is at the very top of a tile.
   * The tile is solid, so dy should be clamped to NOT enter the tile.
   * *dy -= yoffset means "back off by yoffset pixels".  yoffset=0 means
   * the probe is exactly at tile boundary — correct, dy keeps its value
   * because the sprite's bottom would be at Y=112-1=111 (just outside).
   *
   * Let me try dy=4 → Y=113, tile 14, offset 1.  Tile 14 solid:
   *   dy -= 1 = 3.  Now bottom is at Y=112 — touching but not overlapping.
   */
  mock_grid_set(T_LEFT_TX, T_BOTTOM_TY + 1, true);
  mock_grid_set(T_LEFT_TX + 1, T_BOTTOM_TY + 1, true);

  int dx = 0, dy = 4;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &dx, &dy);
  ASSERT_EQ(dy, 3);
  ASSERT_EQ(dx, 0);
}

/* ========================================================================
 * Tests: resolve_collision — diagonal movement
 * ======================================================================== */

TEST(test_move_up_right_empty) {
  mock_grid_clear();
  int dx = 4, dy = -4;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &dx, &dy);
  ASSERT_EQ(dx, 4);
  ASSERT_EQ(dy, -4);
}

TEST(test_move_up_left_empty) {
  mock_grid_clear();
  int dx = -4, dy = -4;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &dx, &dy);
  ASSERT_EQ(dx, -4);
  ASSERT_EQ(dy, -4);
}

TEST(test_move_down_right_empty) {
  mock_grid_clear();
  int dx = 4, dy = 4;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &dx, &dy);
  ASSERT_EQ(dx, 4);
  ASSERT_EQ(dy, 4);
}

TEST(test_move_down_left_empty) {
  mock_grid_clear();
  int dx = -4, dy = 4;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &dx, &dy);
  ASSERT_EQ(dx, -4);
  ASSERT_EQ(dy, 4);
}

TEST(test_move_up_right_wall_right) {
  mock_grid_clear();
  /* Solid wall to the right of the right probe — only within the sprite's
   * vertical extent so the upward v_tile probe at (right_x, sprite_y + dy)
   * does NOT hit the wall (that tile row is above the wall). */
  for (int ty = T_TOP_TY; ty <= T_BOTTOM_TY; ty++)
    mock_grid_set(T_RIGHT_TX, ty, true);

  int dx = 4, dy = -4;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &dx, &dy);
  /* Right wall hit: dx should be clamped to 0 */
  ASSERT_EQ(dx, 0);
  /* dy should still allow upward movement */
  ASSERT_EQ(dy, -4);
}

TEST(test_move_up_right_ceiling) {
  mock_grid_clear();
  /* Solid ceiling above the top probe. */
  mock_grid_set(T_LEFT_TX, T_TOP_TY - 1, true);
  mock_grid_set(T_LEFT_TX + 1, T_TOP_TY - 1, true);
  mock_grid_set(T_RIGHT_TX, T_TOP_TY - 1, true);
  mock_grid_set(T_RIGHT_TX + 1, T_TOP_TY - 1, true);

  int dx = 4, dy = -4;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &dx, &dy);
  /* Ceiling hit: dy should be clamped to 0 */
  ASSERT_EQ(dy, 0);
  /* dx should still allow rightward movement */
  ASSERT_EQ(dx, 4);
}

TEST(test_move_down_left_wall_left) {
  mock_grid_clear();
  /* Solid wall to the left of the left probe. */
  for (int ty = T_TOP_TY - 2; ty <= T_BOTTOM_TY + 2; ty++)
    mock_grid_set(T_LEFT_TX - 1, ty, true);

  int dx = -4, dy = 4;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &dx, &dy);
  ASSERT_EQ(dx, 0);
  ASSERT_EQ(dy, 4);
}

TEST(test_move_down_right_floor) {
  mock_grid_clear();
  /* Solid floor below the bottom probe.
   * Bottom Y = 109.  dy = 4 → Y = 113 → tile 14, offset 1.
   * Make tile row 14 solid across probe columns. */
  for (int tx = T_LEFT_TX - 1; tx <= T_RIGHT_TX + 2; tx++)
    mock_grid_set(tx, T_BOTTOM_TY + 1, true);

  int dx = 4, dy = 4;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &dx, &dy);
  ASSERT_EQ(dy, 3);
  ASSERT_EQ(dx, 4);
}

/* ========================================================================
 * Tests: col_definition_lookup (static)
 * ======================================================================== */

TEST(test_col_def_lookup_out_of_range) {
  /* H or N out of range → no modification */
  int dx = 5, dy = -3;
  col_definition_lookup(0, 5, 0, false, false, &dx, &dy);
  ASSERT_EQ(dx, 5);
  ASSERT_EQ(dy, -3);

  dx = 5;
  dy = -3;
  col_definition_lookup(9, 5, 0, false, false, &dx, &dy);
  ASSERT_EQ(dx, 5);
  ASSERT_EQ(dy, -3);

  dx = 5;
  dy = -3;
  col_definition_lookup(5, 0, 0, false, false, &dx, &dy);
  ASSERT_EQ(dx, 5);
  ASSERT_EQ(dy, -3);

  dx = 5;
  dy = -3;
  col_definition_lookup(5, 10, 0, false, false, &dx, &dy);
  ASSERT_EQ(dx, 5);
  ASSERT_EQ(dy, -3);
}

TEST(test_col_def_lookup_identity_rotation) {
  /* Rotation 0, no mirror — up-right frame (dx>0, dy<0).
   * Set all thresholds to COL_THRESH_NONE (default after init) so everything
   * is "invalid" → the function should clamp dy. */

  /* Re-init the table: all NONE */
  for (int h = 0; h <= COL_DEF_MAX_H; h++)
    for (int n = 0; n <= COL_DEF_MAX_N; n++)
      for (int r = 0; r < COL_DEF_ROWS; r++)
        col_thresh[h][n][r] = COL_THRESH_NONE;

  /* H=4, N=5, row_idx = 8 + dy.  dy=-3 → row=5.
   * thresh=NONE → u(5) < NONE(9) is true → "invalid" → clamp.
   * Clamped: cu=5, cv = H - 8 = 4 - 8 = -4.
   * Forward transform: rot=0, no mirror → (5, -4). */
  int dx = 5, dy = -3;
  col_definition_lookup(4, 5, 0, false, false, &dx, &dy);
  ASSERT_EQ(dx, 5);
  ASSERT_EQ(dy, -4);
}

TEST(test_col_def_lookup_valid_cell) {
  /* Set threshold so that the cell IS valid (V) → no modification. */
  for (int h = 0; h <= COL_DEF_MAX_H; h++)
    for (int n = 0; n <= COL_DEF_MAX_N; n++)
      for (int r = 0; r < COL_DEF_ROWS; r++)
        col_thresh[h][n][r] = COL_THRESH_NONE;

  /* H=4, N=5, dy=-3 → row=5.  Set threshold to 3 (first valid col).
   * u=5 >= 3 → valid → no change. */
  col_thresh[4][5][5] = 3;

  int dx = 5, dy = -3;
  col_definition_lookup(4, 5, 0, false, false, &dx, &dy);
  ASSERT_EQ(dx, 5);
  ASSERT_EQ(dy, -3);
}

TEST(test_col_def_lookup_invalid_cell) {
  for (int h = 0; h <= COL_DEF_MAX_H; h++)
    for (int n = 0; n <= COL_DEF_MAX_N; n++)
      for (int r = 0; r < COL_DEF_ROWS; r++)
        col_thresh[h][n][r] = COL_THRESH_NONE;

  /* H=4, N=5, dy=-3 → row=5.  Set threshold to 7.
   * u=5 < 7 → invalid → clamp dy to H - 8 = -4. */
  col_thresh[4][5][5] = 7;

  int dx = 5, dy = -3;
  col_definition_lookup(4, 5, 0, false, false, &dx, &dy);
  ASSERT_EQ(dx, 5);
  ASSERT_EQ(dy, -4);
}

TEST(test_col_def_lookup_rotation_1) {
  /* Rotation 1 = down-right.  Caller provides dx>0, dy>0.
   * Inverse rotation steps: inv_rot = (4-1)%4 = 3.
   * Three CW steps on (dx,dy):
   *   step 1: (u,v) = (-dy, dx)
   *   step 2: (u,v) = (-dx, -dy)
   *   step 3: (u,v) = (dy, -dx)
   * So after inv_rot: u = dy, v = -dx.
   * For dx=3, dy=5: u=5, v=-3 → up-right frame ✓
   *
   * Use H=4, N=5, row=5, thresh=NONE → invalid → clamp.
   * Clamped (cu,cv) = (5, -4).
   * Forward rot=1 (one CW step): (cu,cv) → (-cv, cu) = (4, 5).
   * No mirror → final (4, 5). */
  for (int h = 0; h <= COL_DEF_MAX_H; h++)
    for (int n = 0; n <= COL_DEF_MAX_N; n++)
      for (int r = 0; r < COL_DEF_ROWS; r++)
        col_thresh[h][n][r] = COL_THRESH_NONE;

  int dx = 3, dy = 5;
  col_definition_lookup(4, 5, 1, false, false, &dx, &dy);
  ASSERT_EQ(dx, 4);
  ASSERT_EQ(dy, 5);
}

TEST(test_col_def_lookup_mirror_h) {
  /* Rotation 0, mirror_h.
   * Caller provides dx, dy.  After mirror_h: u = -dx, v = dy.
   * inv_rot=0 → no rotation.  Need u>0, v<0 → -dx>0, dy<0 → dx<0, dy<0.
   *
   * dx=-5, dy=-3: u=5, v=-3 → up-right frame.
   * H=4, N=5, row=5, thresh=NONE → clamp: cu=5, cv=-4.
   * Forward: rot=0, mirror_h: cu = -5.  Final (-5, -4). */
  for (int h = 0; h <= COL_DEF_MAX_H; h++)
    for (int n = 0; n <= COL_DEF_MAX_N; n++)
      for (int r = 0; r < COL_DEF_ROWS; r++)
        col_thresh[h][n][r] = COL_THRESH_NONE;

  int dx = -5, dy = -3;
  col_definition_lookup(4, 5, 0, true, false, &dx, &dy);
  ASSERT_EQ(dx, -5);
  ASSERT_EQ(dy, -4);
}

/* ========================================================================
 * Tests: load_col_definition
 * ======================================================================== */

TEST(test_load_col_definition_null_file) {
  /* FileOpen returns NULL → table stays at defaults (NONE). */
  mock_file_ptr = NULL;

  /* Re-init to known state first */
  for (int h = 0; h <= COL_DEF_MAX_H; h++)
    for (int n = 0; n <= COL_DEF_MAX_N; n++)
      for (int r = 0; r < COL_DEF_ROWS; r++)
        col_thresh[h][n][r] = 0;

  load_col_definition();

  /* After load with NULL file, table should be all NONE (9) because
   * the function reinitialises to NONE before trying to read. */
  ASSERT_EQ(col_thresh[1][1][0], COL_THRESH_NONE);
  ASSERT_EQ(col_thresh[4][5][3], COL_THRESH_NONE);
}

TEST(test_load_col_definition_basic) {
  /* Create a temp file with a small col_definition snippet. */
  FILE *tf = tmpfile();
  ASSERT_TRUE(tf != NULL);

  /* Format: "# HxN" header, then rows.
   * 'V' marks the first valid column; '-' marks invalid.
   * We test a 2x3 grid (H=2, N=3). */
  fprintf(tf, "# 2x3\n"
              "V, -, -, -, -, -, -, -, -\n" /* row 0: thresh=0 */
              "-, V, -, -, -, -, -, -, -\n" /* row 1: thresh=1 */
              "-, -, V, -, -, -, -, -, -\n" /* row 2: thresh=2 */
              "-, -, -, V, -, -, -, -, -\n" /* row 3: thresh=3 */
              "-, -, -, -, V, -, -, -, -\n" /* row 4: thresh=4 */
              "-, -, -, -, -, V, -, -, -\n" /* row 5: thresh=5 */
              "-, -, -, -, -, -, V, -, -\n" /* row 6: thresh=6 */
              "-, -, -, -, -, -, -, V, -\n" /* row 7: thresh=7 */
  );
  rewind(tf);

  mock_file_ptr = tf;
  load_col_definition();

  ASSERT_EQ(col_thresh[2][3][0], 0);
  ASSERT_EQ(col_thresh[2][3][1], 1);
  ASSERT_EQ(col_thresh[2][3][2], 2);
  ASSERT_EQ(col_thresh[2][3][3], 3);
  ASSERT_EQ(col_thresh[2][3][4], 4);
  ASSERT_EQ(col_thresh[2][3][5], 5);
  ASSERT_EQ(col_thresh[2][3][6], 6);
  ASSERT_EQ(col_thresh[2][3][7], 7);
  /* tf is closed by load_col_definition via fclose() */
}

TEST(test_load_col_definition_skip_comment_and_marker) {
  FILE *tf = tmpfile();
  ASSERT_TRUE(tf != NULL);

  fprintf(tf, "// this is a comment\n"
              "# 1x1\n"
              "* probe row (skipped)\n"
              "P player row (skipped)\n"
              "-, -, -, V, -, -, -, -, -\n" /* row 0: thresh=3 */
              "-, -, -, -, -, -, -, -, -\n" /* row 1: all invalid → NONE */
              "V, -, -, -, -, -, -, -, -\n" /* row 2: thresh=0 */
              "-, -, -, -, -, -, -, -, -\n" /* row 3: NONE */
              "-, -, -, -, -, -, -, -, -\n" /* row 4: NONE */
              "-, -, -, -, -, -, -, -, -\n" /* row 5: NONE */
              "-, -, -, -, -, -, -, -, -\n" /* row 6: NONE */
              "-, V, -, -, -, -, -, -, -\n" /* row 7: thresh=1 */
  );
  rewind(tf);

  mock_file_ptr = tf;
  load_col_definition();

  ASSERT_EQ(col_thresh[1][1][0], 3);
  ASSERT_EQ(col_thresh[1][1][1], COL_THRESH_NONE);
  ASSERT_EQ(col_thresh[1][1][2], 0);
  ASSERT_EQ(col_thresh[1][1][7], 1);
}

TEST(test_load_col_definition_multiple_sections) {
  FILE *tf = tmpfile();
  ASSERT_TRUE(tf != NULL);

  /* Two sections: 1x1 and 3x2 */
  fprintf(tf, "# 1x1\n"
              "V, -, -, -, -, -, -, -, -\n"
              "V, -, -, -, -, -, -, -, -\n"
              "V, -, -, -, -, -, -, -, -\n"
              "V, -, -, -, -, -, -, -, -\n"
              "V, -, -, -, -, -, -, -, -\n"
              "V, -, -, -, -, -, -, -, -\n"
              "V, -, -, -, -, -, -, -, -\n"
              "V, -, -, -, -, -, -, -, -\n"
              "# 3x2\n"
              "-, -, -, -, -, -, -, -, V\n"
              "-, -, -, -, -, -, -, -, V\n"
              "-, -, -, -, -, -, -, -, V\n"
              "-, -, -, -, -, -, -, -, V\n"
              "-, -, -, -, -, -, -, -, V\n"
              "-, -, -, -, -, -, -, -, V\n"
              "-, -, -, -, -, -, -, -, V\n"
              "-, -, -, -, -, -, -, -, V\n");
  rewind(tf);

  mock_file_ptr = tf;
  load_col_definition();

  /* Section 1x1: all rows thresh=0 */
  for (int r = 0; r < COL_DEF_ROWS; r++)
    ASSERT_EQ(col_thresh[1][1][r], 0);

  /* Section 3x2: all rows thresh=8 */
  for (int r = 0; r < COL_DEF_ROWS; r++)
    ASSERT_EQ(col_thresh[3][2][r], 8);
}

/* ========================================================================
 * Tests: edge cases
 * ======================================================================== */

TEST(test_move_left_max_velocity) {
  mock_grid_clear();
  /* Wall one tile to the left. dx = -MAX_VELOCITY = -8.
   * Left probe at pixel 40.  dx=-8 → pixel 32, tile 4, offset 0.
   * Tile 4 solid → dx += TILE_SIZE - 0 = 8 → dx = 0. */
  for (int ty = T_TOP_TY; ty <= T_BOTTOM_TY + 1; ty++)
    mock_grid_set(T_LEFT_TX - 1, ty, true);

  int dx = -8, dy = 0;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &dx, &dy);
  ASSERT_EQ(dx, 0);
}

TEST(test_move_right_max_velocity) {
  mock_grid_clear();
  /* Wall at the tile just past the right probe edge.
   * Right probe at pixel 56.  dx=8 → pixel 64, tile 8, offset 0.
   * Tile 8 solid → dx -= 0 = 8.  (offset 0 means at boundary → allowed)
   *
   * dx=7 → pixel 63, tile 7, offset 7.  Tile 7 solid → dx -= 7 = 0. */
  for (int ty = T_TOP_TY; ty <= T_BOTTOM_TY + 1; ty++)
    mock_grid_set(T_RIGHT_TX, ty, true);

  int dx = 7, dy = 0;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &dx, &dy);
  ASSERT_EQ(dx, 0);
}

TEST(test_move_up_max_velocity) {
  mock_grid_clear();
  /* Ceiling one tile above. Top probe at Y=64.
   * dy=-8 → Y=56, tile 7, offset 0.
   * Tile 7 solid → dy += 8-0 = 8 → dy = 0. */
  mock_grid_set(T_LEFT_TX, T_TOP_TY - 1, true);
  mock_grid_set(T_LEFT_TX + 1, T_TOP_TY - 1, true);

  int dx = 0, dy = -8;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &dx, &dy);
  ASSERT_EQ(dy, 0);
}

TEST(test_single_solid_tile_not_on_probe) {
  mock_grid_clear();
  /* A random solid tile that no probe touches shouldn't affect movement. */
  mock_grid_set(20, 20, true);

  int dx = 4, dy = -3;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &dx, &dy);
  ASSERT_EQ(dx, 4);
  ASSERT_EQ(dy, -3);
}

/* ========================================================================
 * Tests: resolve_collision preserves sign symmetry
 * ======================================================================== */

TEST(test_symmetric_left_right_clamping) {
  /* Symmetric walls: solid tiles on both sides at equal distance.
   * Moving left should clamp the same magnitude as moving right. */

  /* Right wall */
  mock_grid_clear();
  for (int ty = T_TOP_TY; ty <= T_BOTTOM_TY + 1; ty++)
    mock_grid_set(T_RIGHT_TX, ty, true);
  int dx_r = 3, dy_r = 0;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &dx_r, &dy_r);

  /* Left wall */
  mock_grid_clear();
  for (int ty = T_TOP_TY; ty <= T_BOTTOM_TY + 1; ty++)
    mock_grid_set(T_LEFT_TX - 1, ty, true);
  int dx_l = -3, dy_l = 0;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &dx_l, &dy_l);

  /* Both should be clamped to 0 (same tile offset geometry). */
  ASSERT_EQ(dx_r, 0);
  ASSERT_EQ(dx_l, 0);
}

/* ========================================================================
 * main
 * ======================================================================== */

/* =========================================================================
 * Position set B: probes NOT tile-aligned so h/v/hv land on different tiles.
 *
 * world_x=0, sprite_x=33, sprite_y=65
 *   Left  X = 41 (tile 5, off 1)   Right X = 57 (tile 7, off 1)
 *   Top   Y = 65 (tile 8, off 1)   Bottom Y = 110 (tile 13, off 6)
 *
 * UP-RIGHT dx=8 dy=-7:
 *   h: (65,65) T(8,8) off(1,1)  v: (57,58) T(7,7) off(1,2)  hv: (65,58) T(8,7) off(1,2)
 * UP-LEFT dx=-8 dy=-7:
 *   h: (33,65) T(4,8) off(1,1)  v: (41,58) T(5,7) off(1,2)  hv: (33,58) T(4,7) off(1,2)
 * DOWN-RIGHT dx=8 dy=7:
 *   h: (65,109) T(8,13) off(1,5) v: (57,116) T(7,14) off(1,4) hv: (65,116) T(8,14) off(1,4)
 * DOWN-LEFT dx=-8 dy=7:
 *   h: (33,109) T(4,13) off(1,5) v: (41,116) T(5,14) off(1,4) hv: (33,116) T(4,14) off(1,4)
 * ========================================================================= */
#define B_WX 0
#define B_SX 33
#define B_SY 65

/* Shorthand: init table to all-NONE */
static void col_thresh_clear(void) {
  for (int h = 0; h <= COL_DEF_MAX_H; h++)
    for (int n = 0; n <= COL_DEF_MAX_N; n++)
      for (int r = 0; r < COL_DEF_ROWS; r++)
        col_thresh[h][n][r] = COL_THRESH_NONE;
}

/* ========================================================================
 * Additional: load_col_definition edge cases
 * ======================================================================== */

TEST(test_load_col_out_of_range_section) {
  FILE *tf = tmpfile();
  ASSERT_TRUE(tf != NULL);
  /* Section 99x5: H=99 > MAX (8) → data lines skipped */
  fprintf(tf, "# 99x5\n"
              "V, -, -, -, -, -, -, -, -\n"
              "V, -, -, -, -, -, -, -, -\n"
              "V, -, -, -, -, -, -, -, -\n"
              "V, -, -, -, -, -, -, -, -\n"
              "V, -, -, -, -, -, -, -, -\n"
              "V, -, -, -, -, -, -, -, -\n"
              "V, -, -, -, -, -, -, -, -\n"
              "V, -, -, -, -, -, -, -, -\n");
  rewind(tf);
  mock_file_ptr = tf;
  load_col_definition();
  /* All entries should remain NONE because the section was skipped */
  ASSERT_EQ(col_thresh[1][1][0], COL_THRESH_NONE);
}

TEST(test_load_col_overflow_rows) {
  FILE *tf = tmpfile();
  ASSERT_TRUE(tf != NULL);
  /* Section 1x1 with >8 data rows: extra rows hit cur_row >= COL_DEF_ROWS guard */
  fprintf(tf, "# 1x1\n"
              "V, -, -, -, -, -, -, -, -\n" /* 0 */
              "V, -, -, -, -, -, -, -, -\n" /* 1 */
              "V, -, -, -, -, -, -, -, -\n" /* 2 */
              "V, -, -, -, -, -, -, -, -\n" /* 3 */
              "V, -, -, -, -, -, -, -, -\n" /* 4 */
              "V, -, -, -, -, -, -, -, -\n" /* 5 */
              "V, -, -, -, -, -, -, -, -\n" /* 6 */
              "V, -, -, -, -, -, -, -, -\n" /* 7 */
              "V, -, -, -, -, -, -, -, -\n" /* 8 → overflow, skipped */
              "V, -, -, -, -, -, -, -, -\n" /* 9 → overflow, skipped */
  );
  rewind(tf);
  mock_file_ptr = tf;
  load_col_definition();
  ASSERT_EQ(col_thresh[1][1][0], 0);
  ASSERT_EQ(col_thresh[1][1][7], 0);
}

/* ========================================================================
 * Additional: col_definition_lookup
 * ======================================================================== */

TEST(test_col_def_lookup_mirror_v) {
  col_thresh_clear();
  /* Rotation 0, mirror_v. Caller: dx>0, dy>0.
   * After mirror_v: u=dx, v=-dy → up-right frame.
   * dx=5, dy=3: u=5, v=-3.  H=4, N=5, row=5.
   * thresh=NONE → invalid → clamp: cu=5, cv=-4.
   * Forward: rot=0, mirror_v: cv = -(-4) = 4.  Final (5, 4). */
  int dx = 5, dy = 3;
  col_definition_lookup(4, 5, 0, false, true, &dx, &dy);
  ASSERT_EQ(dx, 5);
  ASSERT_EQ(dy, 4);
}

TEST(test_col_def_lookup_rotation_2) {
  col_thresh_clear();
  /* Rotation 2 = down-left.  Caller: dx<0, dy>0.
   * inv_rot = 2.  Two CW steps on (dx,dy):
   *   step 1: (-dy, dx)  step 2: (-dx, -dy)
   * dx=-3, dy=5: u=3, v=-5.  H=4,N=5, row=3.
   * thresh=NONE → clamp: cu=3, cv=-4.
   * Forward 2 CW: step 1: (4,3), step 2: (-3,4).
   * No mirror → final (-3, 4). */
  int dx = -3, dy = 5;
  col_definition_lookup(4, 5, 2, false, false, &dx, &dy);
  ASSERT_EQ(dx, -3);
  ASSERT_EQ(dy, 4);
}

TEST(test_col_def_lookup_rotation_3) {
  col_thresh_clear();
  /* Rotation 3 = up-left.  Caller: dx<0, dy<0.
   * inv_rot = 1.  One CW step on (dx,dy):
   *   u = -dy, v = dx.
   * dx=-3, dy=-5: u=5, v=-3.  H=4,N=5, row=5.
   * thresh=NONE → clamp: cu=5, cv=-4.
   * Forward 3 CW: step 1: (4,5), step 2: (-5,4), step 3: (-4,-5).
   * Final (-4, -5). */
  int dx = -3, dy = -5;
  col_definition_lookup(4, 5, 3, false, false, &dx, &dy);
  ASSERT_EQ(dx, -4);
  ASSERT_EQ(dy, -5);
}

TEST(test_col_def_lookup_row_out_of_range) {
  col_thresh_clear();
  /* dy=-9 → row_idx = 8 + (-9) = -1. Out of range → early return. */
  int dx = 5, dy = -9;
  col_definition_lookup(4, 5, 0, false, false, &dx, &dy);
  ASSERT_EQ(dx, 5);
  ASSERT_EQ(dy, -9);
}

TEST(test_col_def_lookup_wrong_frame) {
  col_thresh_clear();
  /* After inverse rotation, u <= 0 → early return. */
  int dx = -5, dy = -3;
  col_definition_lookup(4, 5, 0, false, false, &dx, &dy);
  ASSERT_EQ(dx, -5);
  ASSERT_EQ(dy, -3);
}

/* ========================================================================
 * Additional: UP-RIGHT corner probe branches (direct static calls)
 * ======================================================================== */

TEST(test_ur_probe_A1b_v_only) {
  mock_grid_clear();
  mock_grid_set(7, 7, true); /* v solid */
  int dx = 8, dy = -7;
  move_up_right_probe_up_right(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, 8);
  ASSERT_EQ(dy, -1); /* -7 + (8-2) = -1 */
}

TEST(test_ur_probe_A2a_h_only) {
  mock_grid_clear();
  mock_grid_set(8, 8, true); /* h solid */
  int dx = 8, dy = -7;
  move_up_right_probe_up_right(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, 7); /* 8 - 1 = 7 */
  ASSERT_EQ(dy, -7);
}

TEST(test_ur_probe_A2b_h_and_v) {
  mock_grid_clear();
  mock_grid_set(8, 8, true); /* h */
  mock_grid_set(7, 7, true); /* v */
  int dx = 8, dy = -7;
  move_up_right_probe_up_right(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, 7);
  ASSERT_EQ(dy, -1);
}

TEST(test_ur_probe_B1_hv_only) {
  mock_grid_clear();
  mock_grid_set(8, 7, true); /* hv only */
  int dx = 8, dy = -7;
  move_up_right_probe_up_right(B_WX, B_SX, B_SY, &dx, &dy);
  /* corner comparison: y_overlap < x_overlap always true → dx clamped */
  ASSERT_EQ(dx, 7); /* 8 - 1 */
  ASSERT_EQ(dy, -7);
}

TEST(test_ur_probe_B2_hv_and_v) {
  mock_grid_clear();
  mock_grid_set(8, 7, true); /* hv */
  mock_grid_set(7, 7, true); /* v */
  int dx = 8, dy = -7;
  move_up_right_probe_up_right(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, 8);
  ASSERT_EQ(dy, -1); /* v clamp only */
}

TEST(test_ur_probe_C_hv_and_h) {
  mock_grid_clear();
  mock_grid_set(8, 7, true); /* hv */
  mock_grid_set(8, 8, true); /* h */
  int dx = 8, dy = -7;
  move_up_right_probe_up_right(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, 7); /* h clamp */
  ASSERT_EQ(dy, -7);
}

TEST(test_ur_probe_D_all_solid) {
  mock_grid_clear();
  mock_grid_set(8, 7, true); /* hv */
  mock_grid_set(8, 8, true); /* h */
  mock_grid_set(7, 7, true); /* v */
  int dx = 8, dy = -7;
  move_up_right_probe_up_right(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, 7);
  ASSERT_EQ(dy, -1);
}

/* ========================================================================
 * Additional: UP-LEFT corner probe branches
 * ======================================================================== */

TEST(test_ul_probe_A1b_v_only) {
  mock_grid_clear();
  mock_grid_set(5, 7, true); /* v at (41,58) T(5,7) */
  int dx = -8, dy = -7;
  move_up_left_probe_up_left(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, -8);
  ASSERT_EQ(dy, -1);
}

TEST(test_ul_probe_A2a_h_only) {
  mock_grid_clear();
  mock_grid_set(4, 8, true); /* h at (33,65) T(4,8) */
  int dx = -8, dy = -7;
  move_up_left_probe_up_left(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, -1); /* -8 + (8-1) */
  ASSERT_EQ(dy, -7);
}

TEST(test_ul_probe_A2b_h_and_v) {
  mock_grid_clear();
  mock_grid_set(4, 8, true); /* h */
  mock_grid_set(5, 7, true); /* v */
  int dx = -8, dy = -7;
  move_up_left_probe_up_left(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, -1);
  ASSERT_EQ(dy, -1);
}

TEST(test_ul_probe_B1_hv_only) {
  mock_grid_clear();
  mock_grid_set(4, 7, true); /* hv at (33,58) T(4,7) */
  int dx = -8, dy = -7;
  move_up_left_probe_up_left(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, -1); /* -8 + (8-1) */
  ASSERT_EQ(dy, -7);
}

TEST(test_ul_probe_B2_hv_and_v) {
  mock_grid_clear();
  mock_grid_set(4, 7, true); /* hv */
  mock_grid_set(5, 7, true); /* v */
  int dx = -8, dy = -7;
  move_up_left_probe_up_left(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, -8);
  ASSERT_EQ(dy, -1);
}

TEST(test_ul_probe_C_hv_and_h) {
  mock_grid_clear();
  mock_grid_set(4, 7, true); /* hv */
  mock_grid_set(4, 8, true); /* h */
  int dx = -8, dy = -7;
  move_up_left_probe_up_left(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, -1);
  ASSERT_EQ(dy, -7);
}

TEST(test_ul_probe_D_all_solid) {
  mock_grid_clear();
  mock_grid_set(4, 7, true); /* hv */
  mock_grid_set(4, 8, true); /* h */
  mock_grid_set(5, 7, true); /* v */
  int dx = -8, dy = -7;
  move_up_left_probe_up_left(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, -1);
  ASSERT_EQ(dy, -1);
}

/* ========================================================================
 * Additional: DOWN-RIGHT corner probe branches
 * ======================================================================== */

TEST(test_dr_probe_A1b_v_only) {
  mock_grid_clear();
  mock_grid_set(7, 14, true); /* v at (57,117) T(7,14) off 5 */
  int dx = 8, dy = 7;
  move_down_right_probe_down_right(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, 8);
  ASSERT_EQ(dy, 2); /* 7 - 5 */
}

TEST(test_dr_probe_A2a_h_only) {
  mock_grid_clear();
  mock_grid_set(8, 13, true); /* h at (65,109) T(8,13) */
  int dx = 8, dy = 7;
  move_down_right_probe_down_right(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, 7);
  ASSERT_EQ(dy, 7);
}

TEST(test_dr_probe_A2b_h_and_v) {
  mock_grid_clear();
  mock_grid_set(8, 13, true); /* h */
  mock_grid_set(7, 14, true); /* v */
  int dx = 8, dy = 7;
  move_down_right_probe_down_right(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, 7);
  ASSERT_EQ(dy, 2);
}

TEST(test_dr_probe_B1_hv_only) {
  mock_grid_clear();
  mock_grid_set(8, 14, true); /* hv at (65,116) T(8,14) */
  int dx = 8, dy = 7;
  move_down_right_probe_down_right(B_WX, B_SX, B_SY, &dx, &dy);
  /* For down-right: y_overlap > x_overlap → B1b → dy clamped */
  ASSERT_EQ(dy, 2);
  ASSERT_EQ(dx, 8);
}

TEST(test_dr_probe_B2_hv_and_v) {
  mock_grid_clear();
  mock_grid_set(8, 14, true); /* hv */
  mock_grid_set(7, 14, true); /* v */
  int dx = 8, dy = 7;
  move_down_right_probe_down_right(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dy, 2);
  ASSERT_EQ(dx, 8);
}

TEST(test_dr_probe_C_hv_and_h) {
  mock_grid_clear();
  mock_grid_set(8, 14, true); /* hv */
  mock_grid_set(8, 13, true); /* h */
  int dx = 8, dy = 7;
  move_down_right_probe_down_right(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, 7);
  ASSERT_EQ(dy, 7);
}

TEST(test_dr_probe_D_all_solid) {
  mock_grid_clear();
  mock_grid_set(8, 14, true);
  mock_grid_set(8, 13, true);
  mock_grid_set(7, 14, true);
  int dx = 8, dy = 7;
  move_down_right_probe_down_right(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, 7);
  ASSERT_EQ(dy, 2);
}

/* ========================================================================
 * Additional: DOWN-LEFT corner probe branches
 * ======================================================================== */

TEST(test_dl_probe_A1b_v_only) {
  mock_grid_clear();
  mock_grid_set(5, 14, true); /* v at (41,117) T(5,14) off 5 */
  int dx = -8, dy = 7;
  move_down_left_probe_down_left(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, -8);
  ASSERT_EQ(dy, 2);
}

TEST(test_dl_probe_A2a_h_only) {
  mock_grid_clear();
  mock_grid_set(4, 13, true); /* h at (33,109) T(4,13) */
  int dx = -8, dy = 7;
  move_down_left_probe_down_left(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, -1); /* -8 + (8-1) */
  ASSERT_EQ(dy, 7);
}

TEST(test_dl_probe_B1_hv_only) {
  mock_grid_clear();
  mock_grid_set(4, 14, true); /* hv at (33,116) T(4,14) */
  int dx = -8, dy = 7;
  move_down_left_probe_down_left(B_WX, B_SX, B_SY, &dx, &dy);
  /* For down-left: y_overlap > |x_overlap| → B1b → dy clamped */
  ASSERT_EQ(dy, 2);
  ASSERT_EQ(dx, -8);
}

TEST(test_dl_probe_C_hv_and_h) {
  mock_grid_clear();
  mock_grid_set(4, 14, true);
  mock_grid_set(4, 13, true);
  int dx = -8, dy = 7;
  move_down_left_probe_down_left(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, -1);
  ASSERT_EQ(dy, 7);
}

TEST(test_dl_probe_D_all_solid) {
  mock_grid_clear();
  mock_grid_set(4, 14, true);
  mock_grid_set(4, 13, true);
  mock_grid_set(5, 14, true);
  int dx = -8, dy = 7;
  move_down_left_probe_down_left(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, -1);
  ASSERT_EQ(dy, 2);
}

/* ========================================================================
 * Additional: middle probes (move_y_*_probes_*)
 * ======================================================================== */

TEST(test_middle_right_h_solid) {
  mock_grid_clear();
  /* Position B, dx=8 → right middle probe at (65, 80) T(8,10). Solid → dx clamped. */
  mock_grid_set(8, 10, true);
  int dx = 8, dy = -7;
  move_y_right_probes_right(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, 7); /* 8 - 1 */
}

TEST(test_middle_right_hv_only_solid) {
  mock_grid_clear();
  /* h at (65,80) T(8,10) empty; hv at (65,73) T(8,9) solid → dx via hv */
  mock_grid_set(8, 9, true);
  int dx = 8, dy = -7;
  move_y_right_probes_right(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, 7);
}

TEST(test_middle_left_h_solid) {
  mock_grid_clear();
  /* Position B, dx=-8.  Left middle i=1: h at (33,80) T(4,10). */
  mock_grid_set(4, 10, true);
  int dx = -8, dy = -7;
  move_y_left_probes_left(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, -1); /* -8 + (8-1) */
}

TEST(test_middle_left_hv_only_solid) {
  mock_grid_clear();
  /* h at (33,80) T(4,10) empty; hv at (33,73) T(4,9) solid */
  mock_grid_set(4, 9, true);
  int dx = -8, dy = -7;
  move_y_left_probes_left(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, -1);
}

/* ========================================================================
 * Additional: secondary diagonal probes
 * ======================================================================== */

TEST(test_ur_secondary_up_left_hv_solid) {
  mock_grid_clear();
  /* move_up_right_probe_up_left: hv at (left_x+dx, sprite_y+dy) = (49,58) T(6,7) */
  mock_grid_set(6, 7, true);
  int dx = 8, dy = -7;
  move_up_right_probe_up_left(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dy, -1); /* -7 + (8-2) */
  ASSERT_EQ(dx, 8);
}

TEST(test_ur_secondary_up_left_v_solid) {
  mock_grid_clear();
  /* v at (left_x, sprite_y+dy) = (41,58) T(5,7). hv empty. */
  mock_grid_set(5, 7, true);
  int dx = 8, dy = -7;
  move_up_right_probe_up_left(B_WX, B_SX, B_SY, &dx, &dy);
  /* Calls col_definition_lookup with out-of-range H/N → no change */
  ASSERT_EQ(dx, 8);
  ASSERT_EQ(dy, -7);
}

TEST(test_ur_secondary_down_right_hv_solid) {
  mock_grid_clear();
  /* move_up_right_probe_down_right:
   * hv at (right_x+dx, bottom_y+dy) = (65, 102) T(8,12) */
  mock_grid_set(8, 12, true);
  int dx = 8, dy = -7;
  move_up_right_probe_down_right(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, 7); /* 8 - 1 */
}

TEST(test_ur_secondary_down_right_h_solid) {
  mock_grid_clear();
  /* h at (right_x+dx, bottom_y) = (65,109) T(8,13). hv empty. */
  mock_grid_set(8, 13, true);
  int dx = 8, dy = -7;
  move_up_right_probe_down_right(B_WX, B_SX, B_SY, &dx, &dy);
  /* col_definition_lookup with out-of-range params → no change */
  ASSERT_EQ(dx, 8);
  ASSERT_EQ(dy, -7);
}

TEST(test_ul_secondary_up_right_hv_solid) {
  mock_grid_clear();
  /* move_up_left_probe_up_right: hv at (right_x+dx, sprite_y+dy)
   * right_x=57, dx=-8: (49,58) T(6,7) */
  mock_grid_set(6, 7, true);
  int dx = -8, dy = -7;
  move_up_left_probe_up_right(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dy, -1);
}

TEST(test_ul_secondary_up_right_v_solid) {
  mock_grid_clear();
  /* v at (right_x, sprite_y+dy) = (57,58) T(7,7). hv empty. */
  mock_grid_set(7, 7, true);
  int dx = -8, dy = -7;
  move_up_left_probe_up_right(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, -8);
  ASSERT_EQ(dy, -7);
}

TEST(test_ul_secondary_down_left_hv_solid) {
  mock_grid_clear();
  /* move_up_left_probe_down_left: hv at (left_x+dx, bottom_y+dy)
   * left_x=41, dx=-8: (33, 102) T(4,12) */
  mock_grid_set(4, 12, true);
  int dx = -8, dy = -7;
  move_up_left_probe_down_left(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, -1); /* -8 + (8-1) */
}

TEST(test_ul_secondary_down_left_h_solid) {
  mock_grid_clear();
  /* h at (left_x+dx, bottom_y) = (33,109) T(4,13). hv empty. */
  mock_grid_set(4, 13, true);
  int dx = -8, dy = -7;
  move_up_left_probe_down_left(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, -8);
  ASSERT_EQ(dy, -7);
}

TEST(test_dr_secondary_down_left_hv_solid) {
  mock_grid_clear();
  /* move_down_right_probe_down_left: hv at (left_x+dx, bottom_y+dy)
   * left_x=41, dx=8, bottom=109, dy=7: (49,116) T(6,14) */
  mock_grid_set(6, 14, true);
  int dx = 8, dy = 7;
  move_down_right_probe_down_left(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dy, 2); /* 7 - 5 */
}

TEST(test_dr_secondary_down_left_v_solid) {
  mock_grid_clear();
  /* v at (left_x, bottom_y+dy) = (41,116) T(5,14). hv empty. */
  mock_grid_set(5, 14, true);
  int dx = 8, dy = 7;
  move_down_right_probe_down_left(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, 8);
  ASSERT_EQ(dy, 7);
}

TEST(test_dr_secondary_up_right_hv_solid) {
  mock_grid_clear();
  /* move_down_right_probe_up_right: hv at (right_x+dx, sprite_y+dy)
   * right_x=57, dx=8, sprite_y=65, dy=7: (65,72) T(8,9) */
  mock_grid_set(8, 9, true);
  int dx = 8, dy = 7;
  move_down_right_probe_up_right(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, 7);
}

TEST(test_dr_secondary_up_right_h_solid) {
  mock_grid_clear();
  /* h at (right_x+dx, sprite_y) = (65,65) T(8,8). hv empty. */
  mock_grid_set(8, 8, true);
  int dx = 8, dy = 7;
  move_down_right_probe_up_right(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, 8);
  ASSERT_EQ(dy, 7);
}

TEST(test_dl_secondary_down_right_hv_solid) {
  mock_grid_clear();
  /* move_down_left_probe_down_right: hv at (right_x+dx, bottom_y+dy)
   * right_x=57, dx=-8, bottom=109, dy=7: (49,116) T(6,14) */
  mock_grid_set(6, 14, true);
  int dx = -8, dy = 7;
  move_down_left_probe_down_right(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dy, 2);
}

TEST(test_dl_secondary_down_right_v_solid) {
  mock_grid_clear();
  /* v at (right_x, bottom_y+dy) = (57,116) T(7,14). hv empty. */
  mock_grid_set(7, 14, true);
  int dx = -8, dy = 7;
  move_down_left_probe_down_right(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, -8);
  ASSERT_EQ(dy, 7);
}

TEST(test_dl_secondary_up_left_hv_solid) {
  mock_grid_clear();
  /* move_down_left_probe_up_left: hv at (left_x+dx, sprite_y+dy)
   * left_x=41, dx=-8, sprite_y=65, dy=7: (33,72) T(4,9) */
  mock_grid_set(4, 9, true);
  int dx = -8, dy = 7;
  move_down_left_probe_up_left(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, -1);
}

TEST(test_dl_secondary_up_left_h_solid) {
  mock_grid_clear();
  /* h at (left_x+dx, sprite_y) = (33,65) T(4,8). hv empty. */
  mock_grid_set(4, 8, true);
  int dx = -8, dy = 7;
  move_down_left_probe_up_left(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, -8);
  ASSERT_EQ(dy, 7);
}

/* ========================================================================
 * Additional: dispatch_single_axis remaining branches
 * ======================================================================== */

TEST(test_dispatch_dx_negative) {
  /* up-left with ceiling → dy zeroed, dx<0 → move_left_probes */
  mock_grid_clear();
  /* Make ceiling tiles (row 7) for left probe area so move_up_left_probe
   * zeros dy via B2 branch. */
  mock_grid_set(5, 7, true); /* v at (40,60)→T(5,7) */
  mock_grid_set(4, 7, true); /* hv at (36,60)→T(4,7) */

  int dx = -4, dy = -4;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &dx, &dy);
  /* dy zeroed, dx<0 → dispatch → move_left_probes */
  ASSERT_EQ(dy, 0);
  ASSERT_TRUE(dx <= 0);
}

TEST(test_dispatch_dy_positive) {
  /* down-right with right wall → dx zeroed, dy>0 → move_down_probes */
  mock_grid_clear();
  /* h at (60,109) T(7,13). Make solid so A2a zeros dx. */
  mock_grid_set(7, 13, true);

  int dx = 4, dy = 4;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &dx, &dy);
  ASSERT_EQ(dx, 0);
  ASSERT_TRUE(dy >= 0);
}

TEST(test_dispatch_both_zeroed) {
  /* Up-right where h and v both clamp their axes to 0 simultaneously.
   * Position: default. dx=4, dy=-4.
   * h at (60,64) T(7,8) off(4,0): dx -= 4 = 0.
   * v at (56,60) T(7,7) off(0,4): dy += 8-4 = 0.
   * hv at (60,60) T(7,7): same tile as v.
   * Make T(7,8) and T(7,7) solid → branch D → both zeroed. */
  mock_grid_clear();
  mock_grid_set(7, 7, true);
  mock_grid_set(7, 8, true);

  int dx = 4, dy = -4;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &dx, &dy);
  ASSERT_EQ(dx, 0);
  ASSERT_EQ(dy, 0);
}

/* ========================================================================
 * Additional: full resolve_collision diagonal with mid-probe hits
 * ======================================================================== */

TEST(test_resolve_ur_mid_probe_zeros_dx) {
  mock_grid_clear();
  /* Position B. First corner probe all empty. Middle probe h at T(8,10) solid
   * → dx clamped to 7. Then dispatch: both nonzero → continue. */
  mock_grid_set(8, 10, true);
  int dx = 8, dy = -7;
  resolve_collision(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, 7);
}

TEST(test_resolve_ul_mid_probe_hit) {
  mock_grid_clear();
  /* Middle left probe at T(4,10) solid → dx clamped. */
  mock_grid_set(4, 10, true);
  int dx = -8, dy = -7;
  resolve_collision(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dx, -1);
}

TEST(test_resolve_dr_full_chain) {
  mock_grid_clear();
  /* Down-right: floor at row 14 → first probe v/hv hit → dy clamped */
  mock_grid_fill(0, 14, 20, 15);
  int dx = 8, dy = 7;
  resolve_collision(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dy, 2);
}

TEST(test_resolve_dl_full_chain) {
  mock_grid_clear();
  /* Down-left: floor at row 14 */
  mock_grid_fill(0, 14, 20, 15);
  int dx = -8, dy = 7;
  resolve_collision(B_WX, B_SX, B_SY, &dx, &dy);
  ASSERT_EQ(dy, 2);
}

int main(void) {
  printf("SimonCollision unit tests\n");
  printf("=========================\n\n");

  printf("[resolve_collision — single axis]\n");
  RUN_TEST(test_zero_displacement);
  RUN_TEST(test_move_right_empty);
  RUN_TEST(test_move_right_solid);
  RUN_TEST(test_move_right_partial_clamp);
  RUN_TEST(test_move_left_empty);
  RUN_TEST(test_move_left_solid);
  RUN_TEST(test_move_up_empty);
  RUN_TEST(test_move_up_solid);
  RUN_TEST(test_move_down_empty);
  RUN_TEST(test_move_down_solid);

  printf("\n[resolve_collision — diagonal]\n");
  RUN_TEST(test_move_up_right_empty);
  RUN_TEST(test_move_up_left_empty);
  RUN_TEST(test_move_down_right_empty);
  RUN_TEST(test_move_down_left_empty);
  RUN_TEST(test_move_up_right_wall_right);
  RUN_TEST(test_move_up_right_ceiling);
  RUN_TEST(test_move_down_left_wall_left);
  RUN_TEST(test_move_down_right_floor);

  printf("\n[col_definition_lookup]\n");
  RUN_TEST(test_col_def_lookup_out_of_range);
  RUN_TEST(test_col_def_lookup_identity_rotation);
  RUN_TEST(test_col_def_lookup_valid_cell);
  RUN_TEST(test_col_def_lookup_invalid_cell);
  RUN_TEST(test_col_def_lookup_rotation_1);
  RUN_TEST(test_col_def_lookup_mirror_h);

  printf("\n[load_col_definition]\n");
  RUN_TEST(test_load_col_definition_null_file);
  RUN_TEST(test_load_col_definition_basic);
  RUN_TEST(test_load_col_definition_skip_comment_and_marker);
  RUN_TEST(test_load_col_definition_multiple_sections);

  printf("\n[edge cases]\n");
  RUN_TEST(test_move_left_max_velocity);
  RUN_TEST(test_move_right_max_velocity);
  RUN_TEST(test_move_up_max_velocity);
  RUN_TEST(test_single_solid_tile_not_on_probe);
  RUN_TEST(test_symmetric_left_right_clamping);

  printf("\n[load_col_definition — edge cases]\n");
  RUN_TEST(test_load_col_out_of_range_section);
  RUN_TEST(test_load_col_overflow_rows);

  printf("\n[col_definition_lookup — extended]\n");
  RUN_TEST(test_col_def_lookup_mirror_v);
  RUN_TEST(test_col_def_lookup_rotation_2);
  RUN_TEST(test_col_def_lookup_rotation_3);
  RUN_TEST(test_col_def_lookup_row_out_of_range);
  RUN_TEST(test_col_def_lookup_wrong_frame);

  printf("\n[up-right corner probe branches]\n");
  RUN_TEST(test_ur_probe_A1b_v_only);
  RUN_TEST(test_ur_probe_A2a_h_only);
  RUN_TEST(test_ur_probe_A2b_h_and_v);
  RUN_TEST(test_ur_probe_B1_hv_only);
  RUN_TEST(test_ur_probe_B2_hv_and_v);
  RUN_TEST(test_ur_probe_C_hv_and_h);
  RUN_TEST(test_ur_probe_D_all_solid);

  printf("\n[up-left corner probe branches]\n");
  RUN_TEST(test_ul_probe_A1b_v_only);
  RUN_TEST(test_ul_probe_A2a_h_only);
  RUN_TEST(test_ul_probe_A2b_h_and_v);
  RUN_TEST(test_ul_probe_B1_hv_only);
  RUN_TEST(test_ul_probe_B2_hv_and_v);
  RUN_TEST(test_ul_probe_C_hv_and_h);
  RUN_TEST(test_ul_probe_D_all_solid);

  printf("\n[down-right corner probe branches]\n");
  RUN_TEST(test_dr_probe_A1b_v_only);
  RUN_TEST(test_dr_probe_A2a_h_only);
  RUN_TEST(test_dr_probe_A2b_h_and_v);
  RUN_TEST(test_dr_probe_B1_hv_only);
  RUN_TEST(test_dr_probe_B2_hv_and_v);
  RUN_TEST(test_dr_probe_C_hv_and_h);
  RUN_TEST(test_dr_probe_D_all_solid);

  printf("\n[down-left corner probe branches]\n");
  RUN_TEST(test_dl_probe_A1b_v_only);
  RUN_TEST(test_dl_probe_A2a_h_only);
  RUN_TEST(test_dl_probe_B1_hv_only);
  RUN_TEST(test_dl_probe_C_hv_and_h);
  RUN_TEST(test_dl_probe_D_all_solid);

  printf("\n[middle probes]\n");
  RUN_TEST(test_middle_right_h_solid);
  RUN_TEST(test_middle_right_hv_only_solid);
  RUN_TEST(test_middle_left_h_solid);
  RUN_TEST(test_middle_left_hv_only_solid);

  printf("\n[secondary probes]\n");
  RUN_TEST(test_ur_secondary_up_left_hv_solid);
  RUN_TEST(test_ur_secondary_up_left_v_solid);
  RUN_TEST(test_ur_secondary_down_right_hv_solid);
  RUN_TEST(test_ur_secondary_down_right_h_solid);
  RUN_TEST(test_ul_secondary_up_right_hv_solid);
  RUN_TEST(test_ul_secondary_up_right_v_solid);
  RUN_TEST(test_ul_secondary_down_left_hv_solid);
  RUN_TEST(test_ul_secondary_down_left_h_solid);
  RUN_TEST(test_dr_secondary_down_left_hv_solid);
  RUN_TEST(test_dr_secondary_down_left_v_solid);
  RUN_TEST(test_dr_secondary_up_right_hv_solid);
  RUN_TEST(test_dr_secondary_up_right_h_solid);
  RUN_TEST(test_dl_secondary_down_right_hv_solid);
  RUN_TEST(test_dl_secondary_down_right_v_solid);
  RUN_TEST(test_dl_secondary_up_left_hv_solid);
  RUN_TEST(test_dl_secondary_up_left_h_solid);

  printf("\n[dispatch single-axis]\n");
  RUN_TEST(test_dispatch_dx_negative);
  RUN_TEST(test_dispatch_dy_positive);
  RUN_TEST(test_dispatch_both_zeroed);

  printf("\n[full resolve diagonal chains]\n");
  RUN_TEST(test_resolve_ur_mid_probe_zeros_dx);
  RUN_TEST(test_resolve_ul_mid_probe_hit);
  RUN_TEST(test_resolve_dr_full_chain);
  RUN_TEST(test_resolve_dl_full_chain);

  printf("\n=========================\n");
  printf("Results: %d/%d passed", tests_passed, tests_run);
  if (tests_failed > 0)
    printf(", %d FAILED", tests_failed);
  printf("\n");

  return tests_failed > 0 ? 1 : 0;
}
