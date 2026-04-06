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
 *   ./scv4/test_collision
 */

#ifndef LIB_EXPORTS
#define LIB_EXPORTS /* TLNAPI → dllexport / visibility, not dllimport */
#endif

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Pre-include headers so their guards prevent re-inclusion from
 * SimonCollision.c */
#include "LoadFile.h"
#include "SimonCollision.h"
#include "Tilengine.h"

typedef struct {
  int tx1, ty1, tx2, ty2;
} TileRect;

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
  if (tile_x >= 0 && tile_x < MOCK_GRID_W && tile_y >= 0 && tile_y < MOCK_GRID_H) {
    mock_grid[tile_y][tile_x] = solid;
  }
}

/* Fill a rectangular region of tiles [tx1,ty1) .. [tx2,ty2) */
static void mock_grid_fill(TileRect rect) {
  for (int ty = rect.ty1; ty < rect.ty2; ty++) {
    for (int tx = rect.tx1; tx < rect.tx2; tx++) {
      mock_grid_set(tx, ty, true);
    }
  }
}

#define TILE_SIZE 8

/* ---- Mock implementation of TLN_GetLayerTile ---- */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters, readability-identifier-length)
TLNAPI bool TLN_GetLayerTile(int nlayer, int x, int y, TLN_TileInfo* info) {
  (void)nlayer;
  memset(info, 0, sizeof(*info));
  if (x < 0 || y < 0) {
    info->empty = true;
    return true;
  }
  int tile_x = x / TILE_SIZE;
  int tile_y = y / TILE_SIZE;
  info->xoffset = x % TILE_SIZE;
  info->yoffset = y % TILE_SIZE;
  if (tile_x >= MOCK_GRID_W || tile_y >= MOCK_GRID_H) {
    info->empty = true;
  } else {
    info->empty = ((!mock_grid[tile_y][tile_x]) != 0);
    if (!info->empty) {
      info->index = 1;
    }
  }
  return true;
}

/* ========================================================================
 * Mock: FileOpen
 * ======================================================================== */
static FILE* mock_file_ptr = NULL;

FILE* FileOpen(const char* filename) {
  (void)filename;
  return mock_file_ptr;
}

/* ========================================================================
 * Include source under test — gives access to all static functions
 * ======================================================================== */
#include "SimonCollision.c" // NOLINT(bugprone-suspicious-include)

/* ========================================================================
 * Minimal test harness
 * ======================================================================== */
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void name(void)

#define RUN_TEST(name)                                                                                                 \
  do {                                                                                                                 \
    tests_run++;                                                                                                       \
    printf("  %-55s ", #name);                                                                                         \
    fflush(stdout);                                                                                                    \
    name();                                                                                                            \
    tests_passed++;                                                                                                    \
    printf("PASS\n");                                                                                                  \
  } while (0)

/* ASSERT_EQ jumps out of the current test on failure via return.
 * The RUN_TEST macro skips the tests_passed++ increment, leaving the
 * count off by one — we track failures separately. */
#define ASSERT_EQ(a, b)                                                                                                \
  do {                                                                                                                 \
    int _actual = (a);                                                                                                 \
    int _expect = (b);                                                                                                 \
    if (_actual != _expect) {                                                                                          \
      printf("FAIL\n    %s:%d: %s == %d, expected %d\n", __FILE__, __LINE__, #a, _actual, _expect);                    \
      tests_failed++;                                                                                                  \
      return;                                                                                                          \
    }                                                                                                                  \
  } while (0)

#define ASSERT_TRUE(cond)                                                                                              \
  do {                                                                                                                 \
    if (!(cond)) {                                                                                                     \
      printf("FAIL\n    %s:%d: %s is false\n", __FILE__, __LINE__, #cond);                                             \
      tests_failed++;                                                                                                  \
      return;                                                                                                          \
    }                                                                                                                  \
  } while (0)

/* ========================================================================
 * Helper: position constants
 *
 * All tests use world_x = 0, sprite_x = 32 so that:
 *   left  probe X = 0 + 32 + 8  = 40   (tile 5, offset 0)
 *   right probe X = 0 + 32 + 23 = 55   (tile 6, offset 7)
 *
 * sprite_y = 65 so that (with SIMON_COL_Y_OFFSET = -1):
 *   top    probe Y = 65 - 1 = 64       (tile 8, offset 0)
 *   bottom probe Y = 65 + 45 = 110     (tile 13, offset 6)
 * ======================================================================== */
#define T_WORLD_X   0
#define T_SPRITE_X  32
#define T_SPRITE_Y  65

/* Tile indices for the probes (pixel / 8) */
#define T_LEFT_TX   5  /* 40 / 8 */
#define T_RIGHT_TX  7  /* first solid column past right probe (55/8 + 1) */
#define T_TOP_TY    8  /* 64 / 8 */
#define T_BOTTOM_TY 13 /* 110 / 8 */

/* ========================================================================
 * Tests: resolve_collision — single-axis movement
 * ======================================================================== */

TEST(test_zero_displacement) {
  mock_grid_clear();
  int displacement_x = 0;
  int displacement_y = 0;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 0);
  ASSERT_EQ(displacement_y, 0);
}

TEST(test_move_right_empty) {
  mock_grid_clear();
  int displacement_x = 4;
  int displacement_y = 0;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 4);
  ASSERT_EQ(displacement_y, 0);
}

TEST(test_move_right_solid) {
  mock_grid_clear();
  /* Place a solid column of tiles at the right probe X + some offset.
   * Right probe is at pixel 56.  With displacement_x = 4 the query hits pixel 60.
   * Tile at 60/8 = 7, offset = 60%8 = 4.
   * Tile 7 starts at pixel 56.  Make tile column 7 solid for all probe rows. */
  for (int ty = T_TOP_TY; ty <= T_BOTTOM_TY + 1; ty++) {
    mock_grid_set(T_RIGHT_TX, ty, true);
  }

  int displacement_x = 4;
  int displacement_y = 0;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &displacement_x, &displacement_y);
  /* displacement_x should be clamped: right probe at pixel 55, query at 59, offset 3 → displacement_x -= 3 = 1 */
  ASSERT_EQ(displacement_x, 1);
  ASSERT_EQ(displacement_y, 0);
}

TEST(test_move_right_partial_clamp) {
  mock_grid_clear();
  /* Right probe at pixel 56.  displacement_x=6 → query pixel 62, tile 7 offset 6.
   * Tile 7 solid → displacement_x -= 6 = 0.  But if tile 8 is solid instead, displacement_x=6
   * hits pixel 62 (still tile 7, empty).  Let's put tile 8 (pixel 64):
   * displacement_x=8 → pixel 64, tile 8, offset 0 → displacement_x -= 0 = 8. Still passes.
   *
   * Better: tile 7 solid, displacement_x=2 → pixel 58, offset 2 → displacement_x -= 2 = 0 */
  for (int ty = T_TOP_TY; ty <= T_BOTTOM_TY + 1; ty++) {
    mock_grid_set(T_RIGHT_TX, ty, true);
  }

  int displacement_x = 2;
  int displacement_y = 0;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 1);
  ASSERT_EQ(displacement_y, 0);
}

TEST(test_move_left_empty) {
  mock_grid_clear();
  int displacement_x = -4;
  int displacement_y = 0;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, -4);
  ASSERT_EQ(displacement_y, 0);
}

TEST(test_move_left_solid) {
  mock_grid_clear();
  /* Left probe at pixel 40 (tile 5, offset 0).
   * displacement_x = -4 → query pixel 36, tile 4, offset 4.
   * Make tile 4 solid.
   * Correction: displacement_x += TILE_SIZE - xoffset = -4 + (8 - 4) = 0 */
  for (int ty = T_TOP_TY; ty <= T_BOTTOM_TY + 1; ty++) {
    mock_grid_set(T_LEFT_TX - 1, ty, true);
  }

  int displacement_x = -4;
  int displacement_y = 0;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 0);
  ASSERT_EQ(displacement_y, 0);
}

TEST(test_move_up_empty) {
  mock_grid_clear();
  int displacement_x = 0;
  int displacement_y = -4;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 0);
  ASSERT_EQ(displacement_y, -4);
}

TEST(test_move_up_solid) {
  mock_grid_clear();
  /* Top probe at pixel Y=64 (tile 8, offset 0).
   * displacement_y = -4 → query Y=60, tile 7, offset 4.
   * Make tile row 7 solid at both probe X columns.
   * Correction: displacement_y += TILE_SIZE - yoffset = -4 + (8 - 4) = 0 */
  mock_grid_set(T_LEFT_TX, T_TOP_TY - 1, true);
  mock_grid_set(T_LEFT_TX + 1, T_TOP_TY - 1, true);

  int displacement_x = 0;
  int displacement_y = -4;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 0);
  ASSERT_EQ(displacement_y, 0);
}

TEST(test_move_down_empty) {
  mock_grid_clear();
  int displacement_x = 0;
  int displacement_y = 4;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 0);
  ASSERT_EQ(displacement_y, 4);
}

TEST(test_move_down_solid) {
  mock_grid_clear();
  /* Bottom probe at pixel Y=109 (tile 13, offset 5).
   * displacement_y = 3 → query Y=112, tile 14, offset 0.
   * Make tile row 14 solid at both X columns.
   * Correction: displacement_y -= yoffset = 3 − 0 = 3.  That's unchanged because offset is
   * 0.
   *
   * Try: displacement_y = 4 → Y=113, tile 14, offset 1 → displacement_y -= 1 = 3.
   * Or: make tile 13 solid (bottom probe is already at Y=109 in tile 13).
   *  Then displacement_y=1 → Y=110, tile 13, offset 6 → displacement_y -= 6… that's -5, wrong.
   *
   * Re-read the code:  move_down_probes checks
   *   SIMON_COL_BOTTOM_Y(sprite_y) + *displacement_y  =  109 + displacement_y.
   * If displacement_y=3 → Y=112 → tile=14, yoffset=0.  Tile 14 solid:
   *   *displacement_y -= v_tile.yoffset → displacement_y -= 0 = 3.  No change!
   *
   * The semantics: yoffset=0 means the probe is at the very top of a tile.
   * The tile is solid, so displacement_y should be clamped to NOT enter the tile.
   * *displacement_y -= yoffset means "back off by yoffset pixels".  yoffset=0 means
   * the probe is exactly at tile boundary — correct, displacement_y keeps its value
   * because the sprite's bottom would be at Y=112-1=111 (just outside).
   *
   * Let me try displacement_y=4 → Y=113, tile 14, offset 1.  Tile 14 solid:
   *   displacement_y -= 1 = 3.  Now bottom is at Y=112 — touching but not overlapping.
   */
  mock_grid_set(T_LEFT_TX, T_BOTTOM_TY + 1, true);
  mock_grid_set(T_LEFT_TX + 1, T_BOTTOM_TY + 1, true);

  int displacement_x = 0;
  int displacement_y = 4;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_y, 1);
  ASSERT_EQ(displacement_x, 0);
}

/* ========================================================================
 * Tests: resolve_collision — diagonal movement
 * ======================================================================== */

TEST(test_move_up_right_empty) {
  mock_grid_clear();
  int displacement_x = 4;
  int displacement_y = -4;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 4);
  ASSERT_EQ(displacement_y, -4);
}

TEST(test_move_up_left_empty) {
  mock_grid_clear();
  int displacement_x = -4;
  int displacement_y = -4;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, -4);
  ASSERT_EQ(displacement_y, -4);
}

TEST(test_move_down_right_empty) {
  mock_grid_clear();
  int displacement_x = 4;
  int displacement_y = 4;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 4);
  ASSERT_EQ(displacement_y, 4);
}

TEST(test_move_down_left_empty) {
  mock_grid_clear();
  int displacement_x = -4;
  int displacement_y = 4;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, -4);
  ASSERT_EQ(displacement_y, 4);
}

TEST(test_move_up_right_wall_right) {
  mock_grid_clear();
  /* Solid wall to the right of the right probe — only within the sprite's
   * vertical extent so the upward v_tile probe at (right_x, sprite_y + displacement_y)
   * does NOT hit the wall (that tile row is above the wall). */
  for (int ty = T_TOP_TY; ty <= T_BOTTOM_TY; ty++) {
    mock_grid_set(T_RIGHT_TX, ty, true);
  }

  int displacement_x = 4;
  int displacement_y = -4;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &displacement_x, &displacement_y);
  /* Right wall hit: displacement_x should be clamped to 1 */
  ASSERT_EQ(displacement_x, 1);
  /* displacement_y should still allow upward movement */
  ASSERT_EQ(displacement_y, -4);
}

TEST(test_move_up_right_ceiling) {
  mock_grid_clear();
  /* Solid ceiling above the top probe. */
  mock_grid_set(T_LEFT_TX, T_TOP_TY - 1, true);
  mock_grid_set(T_LEFT_TX + 1, T_TOP_TY - 1, true);
  mock_grid_set(T_RIGHT_TX, T_TOP_TY - 1, true);
  mock_grid_set(T_RIGHT_TX + 1, T_TOP_TY - 1, true);

  int displacement_x = 4;
  int displacement_y = -4;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &displacement_x, &displacement_y);
  /* Ceiling hit: displacement_y should be clamped to 0 */
  ASSERT_EQ(displacement_y, 0);
  /* displacement_x should still allow rightward movement */
  ASSERT_EQ(displacement_x, 4);
}

TEST(test_move_down_left_wall_left) {
  mock_grid_clear();
  /* Solid wall to the left of the left probe. */
  for (int ty = T_TOP_TY - 2; ty <= T_BOTTOM_TY + 2; ty++) {
    mock_grid_set(T_LEFT_TX - 1, ty, true);
  }

  int displacement_x = -4;
  int displacement_y = 4;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 0);
  ASSERT_EQ(displacement_y, 4);
}

TEST(test_move_down_right_floor) {
  mock_grid_clear();
  /* Solid floor below the bottom probe.
   * Bottom Y = 109.  displacement_y = 4 → Y = 113 → tile 14, offset 1.
   * Make tile row 14 solid across probe columns. */
  for (int tx = T_LEFT_TX - 1; tx <= T_RIGHT_TX + 2; tx++) {
    mock_grid_set(tx, T_BOTTOM_TY + 1, true);
  }

  int displacement_x = 4;
  int displacement_y = 4;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_y, 1);
  ASSERT_EQ(displacement_x, 4);
}

/* ========================================================================
 * Tests: col_definition_lookup (static)
 * ======================================================================== */

TEST(test_col_def_lookup_out_of_range) {
  /* H or N out of range → no modification */
  int displacement_x = 5;
  int displacement_y = -3;
  col_definition_lookup(0, 5, 0, false, false, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 5);
  ASSERT_EQ(displacement_y, -3);

  displacement_x = 5;
  displacement_y = -3;
  col_definition_lookup(9, 5, 0, false, false, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 5);
  ASSERT_EQ(displacement_y, -3);

  displacement_x = 5;
  displacement_y = -3;
  col_definition_lookup(5, 0, 0, false, false, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 5);
  ASSERT_EQ(displacement_y, -3);

  displacement_x = 5;
  displacement_y = -3;
  col_definition_lookup(5, 10, 0, false, false, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 5);
  ASSERT_EQ(displacement_y, -3);
}

TEST(test_col_def_lookup_identity_rotation) {
  /* Rotation 0, no mirror — up-right frame (displacement_x>0, displacement_y<0).
   * Set all thresholds to COL_THRESH_NONE (default after init) so everything
   * is "invalid" → the function should clamp displacement_y. */

  /* Re-init the table: all NONE */
  for (int h = 0; h <= COL_DEF_MAX_H; h++) {
    for (int n = 0; n <= COL_DEF_MAX_N; n++) {
      for (int r = 0; r < COL_DEF_ROWS; r++) {
        col_thresh[h][n][r] = COL_THRESH_NONE;
      }
    }
  }

  /* H=4, N=5, row_idx = 8 + displacement_y.  displacement_y=-3 → row=5.
   * thresh=NONE → u(5) < NONE(9) is true → "invalid" → clamp.
   * Clamped: cu=5, cv = H - 8 = 4 - 8 = -4.
   * Forward transform: rot=0, no mirror → (5, -4). */
  int displacement_x = 5;
  int displacement_y = -3;
  col_definition_lookup(4, 5, 0, false, false, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 5);
  ASSERT_EQ(displacement_y, -4);
}

TEST(test_col_def_lookup_valid_cell) {
  /* Set threshold so that the cell IS valid (V) → no modification. */
  for (int h = 0; h <= COL_DEF_MAX_H; h++) {
    for (int n = 0; n <= COL_DEF_MAX_N; n++) {
      for (int r = 0; r < COL_DEF_ROWS; r++) {
        col_thresh[h][n][r] = COL_THRESH_NONE;
      }
    }
  }

  /* H=4, N=5, displacement_y=-3 → row=5.  Set threshold to 3 (first valid col).
   * u=5 >= 3 → valid → no change. */
  col_thresh[4][5][5] = 3;

  int displacement_x = 5;
  int displacement_y = -3;
  col_definition_lookup(4, 5, 0, false, false, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 5);
  ASSERT_EQ(displacement_y, -3);
}

TEST(test_col_def_lookup_invalid_cell) {
  for (int h = 0; h <= COL_DEF_MAX_H; h++) {
    for (int n = 0; n <= COL_DEF_MAX_N; n++) {
      for (int r = 0; r < COL_DEF_ROWS; r++) {
        col_thresh[h][n][r] = COL_THRESH_NONE;
      }
    }
  }

  /* H=4, N=5, displacement_y=-3 → row=5.  Set threshold to 7.
   * u=5 < 7 → invalid → clamp displacement_y to H - 8 = -4. */
  col_thresh[4][5][5] = 7;

  int displacement_x = 5;
  int displacement_y = -3;
  col_definition_lookup(4, 5, 0, false, false, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 5);
  ASSERT_EQ(displacement_y, -4);
}

TEST(test_col_def_lookup_rotation_1) {
  /* Rotation 1 = down-right.  Caller provides displacement_x>0, displacement_y>0.
   * Inverse rotation steps: inv_rot = (4-1)%4 = 3.
   * Three CW steps on (displacement_x,displacement_y):
   *   step 1: (u,v) = (-displacement_y, displacement_x)
   *   step 2: (u,v) = (-displacement_x, -displacement_y)
   *   step 3: (u,v) = (displacement_y, -displacement_x)
   * So after inv_rot: u = displacement_y, v = -displacement_x.
   * For displacement_x=3, displacement_y=5: u=5, v=-3 → up-right frame ✓
   *
   * Use H=4, N=5, row=5, thresh=NONE → invalid → clamp.
   * Clamped (cu,cv) = (5, -4).
   * Forward rot=1 (one CW step): (cu,cv) → (-cv, cu) = (4, 5).
   * No mirror → final (4, 5). */
  for (int h = 0; h <= COL_DEF_MAX_H; h++) {
    for (int n = 0; n <= COL_DEF_MAX_N; n++) {
      for (int r = 0; r < COL_DEF_ROWS; r++) {
        col_thresh[h][n][r] = COL_THRESH_NONE;
      }
    }
  }

  int displacement_x = 3;
  int displacement_y = 5;
  col_definition_lookup(4, 5, 1, false, false, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 4);
  ASSERT_EQ(displacement_y, 5);
}

TEST(test_col_def_lookup_mirror_h) {
  /* Rotation 0, mirror_h.
   * Caller provides displacement_x, displacement_y.  After mirror_h: u = -displacement_x, v = displacement_y.
   * inv_rot=0 → no rotation.  Need u>0, v<0 → -displacement_x>0, displacement_y<0 → displacement_x<0, displacement_y<0.
   *
   * displacement_x=-5, displacement_y=-3: u=5, v=-3 → up-right frame.
   * H=4, N=5, row=5, thresh=NONE → clamp: cu=5, cv=-4.
   * Forward: rot=0, mirror_h: cu = -5.  Final (-5, -4). */
  for (int h = 0; h <= COL_DEF_MAX_H; h++) {
    for (int n = 0; n <= COL_DEF_MAX_N; n++) {
      for (int r = 0; r < COL_DEF_ROWS; r++) {
        col_thresh[h][n][r] = COL_THRESH_NONE;
      }
    }
  }

  int displacement_x = -5;
  int displacement_y = -3;
  col_definition_lookup(4, 5, 0, true, false, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, -5);
  ASSERT_EQ(displacement_y, -4);
}

/* ========================================================================
 * Tests: load_col_definition
 * ======================================================================== */

TEST(test_load_col_definition_null_file) {
  /* FileOpen returns NULL → table stays at defaults (NONE). */
  mock_file_ptr = NULL;

  /* Re-init to known state first */
  for (int h = 0; h <= COL_DEF_MAX_H; h++) {
    for (int n = 0; n <= COL_DEF_MAX_N; n++) {
      for (int r = 0; r < COL_DEF_ROWS; r++) {
        col_thresh[h][n][r] = 0;
      }
    }
  }

  load_col_definition();

  /* After load with NULL file, table should be all NONE (9) because
   * the function reinitialises to NONE before trying to read. */
  ASSERT_EQ(col_thresh[1][1][0], COL_THRESH_NONE);
  ASSERT_EQ(col_thresh[4][5][3], COL_THRESH_NONE);
}

TEST(test_load_col_definition_basic) {
  /* Create a temp file with a small col_definition snippet. */
  FILE* tf = tmpfile();
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
  FILE* tf = tmpfile();
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
  FILE* tf = tmpfile();
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
  for (int r = 0; r < COL_DEF_ROWS; r++) {
    ASSERT_EQ(col_thresh[1][1][r], 0);
  }

  /* Section 3x2: all rows thresh=8 */
  for (int r = 0; r < COL_DEF_ROWS; r++) {
    ASSERT_EQ(col_thresh[3][2][r], 8);
  }
}

/* ========================================================================
 * Tests: edge cases
 * ======================================================================== */

TEST(test_move_left_max_velocity) {
  mock_grid_clear();
  /* Wall one tile to the left. displacement_x = -MAX_VELOCITY = -8.
   * Left probe at pixel 40.  displacement_x=-8 → pixel 32, tile 4, offset 0.
   * Tile 4 solid → displacement_x += TILE_SIZE - 0 = 8 → displacement_x = 0. */
  for (int ty = T_TOP_TY; ty <= T_BOTTOM_TY + 1; ty++) {
    mock_grid_set(T_LEFT_TX - 1, ty, true);
  }

  int displacement_x = -8;
  int displacement_y = 0;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 0);
}

TEST(test_move_right_max_velocity) {
  mock_grid_clear();
  /* Wall at the tile just past the right probe edge.
   * Right probe at pixel 56.  displacement_x=8 → pixel 64, tile 8, offset 0.
   * Tile 8 solid → displacement_x -= 0 = 8.  (offset 0 means at boundary → allowed)
   *
   * displacement_x=7 → pixel 63, tile 7, offset 7.  Tile 7 solid → displacement_x -= 7 = 0. */
  for (int ty = T_TOP_TY; ty <= T_BOTTOM_TY + 1; ty++) {
    mock_grid_set(T_RIGHT_TX, ty, true);
  }

  int displacement_x = 7;
  int displacement_y = 0;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 1);
}

TEST(test_move_up_max_velocity) {
  mock_grid_clear();
  /* Ceiling one tile above. Top probe at Y=64.
   * displacement_y=-8 → Y=56, tile 7, offset 0.
   * Tile 7 solid → displacement_y += 8-0 = 8 → displacement_y = 0. */
  mock_grid_set(T_LEFT_TX, T_TOP_TY - 1, true);
  mock_grid_set(T_LEFT_TX + 1, T_TOP_TY - 1, true);

  int displacement_x = 0;
  int displacement_y = -8;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_y, 0);
}

TEST(test_single_solid_tile_not_on_probe) {
  mock_grid_clear();
  /* A random solid tile that no probe touches shouldn't affect movement. */
  mock_grid_set(20, 20, true);

  int displacement_x = 4;
  int displacement_y = -3;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 4);
  ASSERT_EQ(displacement_y, -3);
}

/* ========================================================================
 * Tests: resolve_collision preserves sign symmetry
 * ======================================================================== */

TEST(test_symmetric_left_right_clamping) {
  /* Symmetric walls: solid tiles on both sides at equal distance.
   * Moving left should clamp the same magnitude as moving right. */

  /* Right wall */
  mock_grid_clear();
  for (int ty = T_TOP_TY; ty <= T_BOTTOM_TY + 1; ty++) {
    mock_grid_set(T_RIGHT_TX, ty, true);
  }
  int dx_r = 3;
  int dy_r = 0;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &dx_r, &dy_r);

  /* Left wall */
  mock_grid_clear();
  for (int ty = T_TOP_TY; ty <= T_BOTTOM_TY + 1; ty++) {
    mock_grid_set(T_LEFT_TX - 1, ty, true);
  }
  int dx_l = -3;
  int dy_l = 0;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &dx_l, &dy_l);

  /* Both should be clamped (left to 0, right to 1 due to probe offset). */
  ASSERT_EQ(dx_r, 1);
  ASSERT_EQ(dx_l, 0);
}

/* ========================================================================
 * main
 * ======================================================================== */

/* =========================================================================
 * Position set B: probes NOT tile-aligned so h/v/hv land on different tiles.
 *
 * world_x=0, sprite_x=34, sprite_y=66
 *   Left  X = 42 (tile 5, off 2)   Right X = 57 (tile 7, off 1)
 *   Top   Y = 65 (tile 8, off 1)   Bottom Y = 111 (tile 13, off 7)
 *
 * UP-RIGHT displacement_x=8 displacement_y=-7:
 *   h: (65,65) T(8,8) off(1,1)  v: (57,58) T(7,7) off(1,2)  hv: (65,58) T(8,7)
 * off(1,2) UP-LEFT displacement_x=-8 displacement_y=-7: h: (34,65) T(4,8) off(2,1)  v: (42,58) T(5,7)
 * off(2,2)  hv: (34,58) T(4,7) off(2,2) DOWN-RIGHT displacement_x=8 displacement_y=7: h: (65,111)
 * T(8,13) off(1,7) v: (57,118) T(7,14) off(1,6) hv: (65,118) T(8,14) off(1,6)
 * DOWN-LEFT displacement_x=-8 displacement_y=7:
 *   h: (34,111) T(4,13) off(2,7) v: (42,118) T(5,14) off(2,6) hv: (34,118)
 * T(4,14) off(2,6)
 * ========================================================================= */
#define B_WX 0
#define B_SX 34
#define B_SY 66

/* Shorthand: init table to all-NONE */
static void col_thresh_clear(void) {
  for (int h = 0; h <= COL_DEF_MAX_H; h++) {
    for (int n = 0; n <= COL_DEF_MAX_N; n++) {
      for (int r = 0; r < COL_DEF_ROWS; r++) {
        col_thresh[h][n][r] = COL_THRESH_NONE;
      }
    }
  }
}

/* ========================================================================
 * Additional: load_col_definition edge cases
 * ======================================================================== */

TEST(test_load_col_out_of_range_section) {
  FILE* tf = tmpfile();
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
  FILE* tf = tmpfile();
  ASSERT_TRUE(tf != NULL);
  /* Section 1x1 with >8 data rows: extra rows hit cur_row >= COL_DEF_ROWS guard
   */
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
  /* Rotation 0, mirror_v. Caller: displacement_x>0, displacement_y>0.
   * After mirror_v: u=displacement_x, v=-displacement_y → up-right frame.
   * displacement_x=5, displacement_y=3: u=5, v=-3.  H=4, N=5, row=5.
   * thresh=NONE → invalid → clamp: cu=5, cv=-4.
   * Forward: rot=0, mirror_v: cv = -(-4) = 4.  Final (5, 4). */
  int displacement_x = 5;
  int displacement_y = 3;
  col_definition_lookup(4, 5, 0, false, true, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 5);
  ASSERT_EQ(displacement_y, 4);
}

TEST(test_col_def_lookup_rotation_2) {
  col_thresh_clear();
  /* Rotation 2 = down-left.  Caller: displacement_x<0, displacement_y>0.
   * inv_rot = 2.  Two CW steps on (displacement_x,displacement_y):
   *   step 1: (-displacement_y, displacement_x)  step 2: (-displacement_x, -displacement_y)
   * displacement_x=-3, displacement_y=5: u=3, v=-5.  H=4,N=5, row=3.
   * thresh=NONE → clamp: cu=3, cv=-4.
   * Forward 2 CW: step 1: (4,3), step 2: (-3,4).
   * No mirror → final (-3, 4). */
  int displacement_x = -3;
  int displacement_y = 5;
  col_definition_lookup(4, 5, 2, false, false, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, -3);
  ASSERT_EQ(displacement_y, 4);
}

TEST(test_col_def_lookup_rotation_3) {
  col_thresh_clear();
  /* Rotation 3 = up-left.  Caller: displacement_x<0, displacement_y<0.
   * inv_rot = 1.  One CW step on (displacement_x,displacement_y):
   *   u = -displacement_y, v = displacement_x.
   * displacement_x=-3, displacement_y=-5: u=5, v=-3.  H=4,N=5, row=5.
   * thresh=NONE → clamp: cu=5, cv=-4.
   * Forward 3 CW: step 1: (4,5), step 2: (-5,4), step 3: (-4,-5).
   * Final (-4, -5). */
  int displacement_x = -3;
  int displacement_y = -5;
  col_definition_lookup(4, 5, 3, false, false, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, -4);
  ASSERT_EQ(displacement_y, -5);
}

TEST(test_col_def_lookup_row_out_of_range) {
  col_thresh_clear();
  /* displacement_y=-9 → row_idx = 8 + (-9) = -1. Out of range → early return. */
  int displacement_x = 5;
  int displacement_y = -9;
  col_definition_lookup(4, 5, 0, false, false, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 5);
  ASSERT_EQ(displacement_y, -9);
}

TEST(test_col_def_lookup_wrong_frame) {
  col_thresh_clear();
  /* After inverse rotation, u <= 0 → early return. */
  int displacement_x = -5;
  int displacement_y = -3;
  col_definition_lookup(4, 5, 0, false, false, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, -5);
  ASSERT_EQ(displacement_y, -3);
}

/* ========================================================================
 * Additional: UP-RIGHT corner probe branches (direct static calls)
 * ======================================================================== */

TEST(test_ur_probe_a1b_v_only) {
  mock_grid_clear();
  mock_grid_set(7, 7, true); /* v solid */
  int displacement_x = 8;
  int displacement_y = -7;
  move_up_right_probe_up_right(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 8);
  ASSERT_EQ(displacement_y, -1); /* -7 + (8-2) = -1 */
}

TEST(test_ur_probe_a2a_h_only) {
  mock_grid_clear();
  mock_grid_set(8, 8, true); /* h solid */
  int displacement_x = 8;
  int displacement_y = -7;
  move_up_right_probe_up_right(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 7); /* 8 - 1 = 7 */
  ASSERT_EQ(displacement_y, -7);
}

TEST(test_ur_probe_a2b_h_and_v) {
  mock_grid_clear();
  mock_grid_set(8, 8, true); /* h */
  mock_grid_set(7, 7, true); /* v */
  int displacement_x = 8;
  int displacement_y = -7;
  move_up_right_probe_up_right(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 7);
  ASSERT_EQ(displacement_y, -1);
}

TEST(test_ur_probe_b1_hv_only) {
  mock_grid_clear();
  mock_grid_set(8, 7, true); /* hv only */
  int displacement_x = 8;
  int displacement_y = -7;
  move_up_right_probe_up_right(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  /* corner comparison: y_overlap < x_overlap always true → displacement_x clamped */
  ASSERT_EQ(displacement_x, 7); /* 8 - 1 */
  ASSERT_EQ(displacement_y, -7);
}

TEST(test_ur_probe_b2_hv_and_v) {
  mock_grid_clear();
  mock_grid_set(8, 7, true); /* hv */
  mock_grid_set(7, 7, true); /* v */
  int displacement_x = 8;
  int displacement_y = -7;
  move_up_right_probe_up_right(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 8);
  ASSERT_EQ(displacement_y, -1); /* v clamp only */
}

TEST(test_ur_probe_c_hv_and_h) {
  mock_grid_clear();
  mock_grid_set(8, 7, true); /* hv */
  mock_grid_set(8, 8, true); /* h */
  int displacement_x = 8;
  int displacement_y = -7;
  move_up_right_probe_up_right(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 7); /* h clamp */
  ASSERT_EQ(displacement_y, -7);
}

TEST(test_ur_probe_d_all_solid) {
  mock_grid_clear();
  mock_grid_set(8, 7, true); /* hv */
  mock_grid_set(8, 8, true); /* h */
  mock_grid_set(7, 7, true); /* v */
  int displacement_x = 8;
  int displacement_y = -7;
  move_up_right_probe_up_right(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 7);
  ASSERT_EQ(displacement_y, -1);
}

/* ========================================================================
 * Additional: UP-LEFT corner probe branches
 * ======================================================================== */

TEST(test_ul_probe_a1b_v_only) {
  mock_grid_clear();
  mock_grid_set(5, 7, true); /* v at (41,58) T(5,7) */
  int displacement_x = -8;
  int displacement_y = -7;
  move_up_left_probe_up_left(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, -8);
  ASSERT_EQ(displacement_y, -1);
}

TEST(test_ul_probe_a2a_h_only) {
  mock_grid_clear();
  mock_grid_set(4, 8, true); /* h at (34,65) T(4,8) */
  int displacement_x = -8;
  int displacement_y = -7;
  move_up_left_probe_up_left(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, -2); /* -8 + (8-2) */
  ASSERT_EQ(displacement_y, -7);
}

TEST(test_ul_probe_a2b_h_and_v) {
  mock_grid_clear();
  mock_grid_set(4, 8, true); /* h */
  mock_grid_set(5, 7, true); /* v */
  int displacement_x = -8;
  int displacement_y = -7;
  move_up_left_probe_up_left(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, -2);
  ASSERT_EQ(displacement_y, -1);
}

TEST(test_ul_probe_b1_hv_only) {
  mock_grid_clear();
  mock_grid_set(4, 7, true); /* hv at (34,58) T(4,7) */
  int displacement_x = -8;
  int displacement_y = -7;
  move_up_left_probe_up_left(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, -2); /* -8 + (8-2) */
  ASSERT_EQ(displacement_y, -7);
}

TEST(test_ul_probe_b2_hv_and_v) {
  mock_grid_clear();
  mock_grid_set(4, 7, true); /* hv */
  mock_grid_set(5, 7, true); /* v */
  int displacement_x = -8;
  int displacement_y = -7;
  move_up_left_probe_up_left(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, -8);
  ASSERT_EQ(displacement_y, -1);
}

TEST(test_ul_probe_c_hv_and_h) {
  mock_grid_clear();
  mock_grid_set(4, 7, true); /* hv */
  mock_grid_set(4, 8, true); /* h */
  int displacement_x = -8;
  int displacement_y = -7;
  move_up_left_probe_up_left(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, -2);
  ASSERT_EQ(displacement_y, -7);
}

TEST(test_ul_probe_d_all_solid) {
  mock_grid_clear();
  mock_grid_set(4, 7, true); /* hv */
  mock_grid_set(4, 8, true); /* h */
  mock_grid_set(5, 7, true); /* v */
  int displacement_x = -8;
  int displacement_y = -7;
  move_up_left_probe_up_left(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, -2);
  ASSERT_EQ(displacement_y, -1);
}

/* ========================================================================
 * Additional: DOWN-RIGHT corner probe branches
 * ======================================================================== */

TEST(test_dr_probe_a1b_v_only) {
  mock_grid_clear();
  mock_grid_set(7, 14, true); /* v at (57,118) T(7,14) off 6 */
  int displacement_x = 8;
  int displacement_y = 7;
  move_down_right_probe_down_right(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 8);
  ASSERT_EQ(displacement_y, 0); /* 7 - (6 + 1) */
}

TEST(test_dr_probe_a2a_h_only) {
  mock_grid_clear();
  mock_grid_set(8, 13, true); /* h at (65,109) T(8,13) */
  int displacement_x = 8;
  int displacement_y = 7;
  move_down_right_probe_down_right(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 7);
  ASSERT_EQ(displacement_y, 7);
}

TEST(test_dr_probe_a2b_h_and_v) {
  mock_grid_clear();
  mock_grid_set(8, 13, true); /* h */
  mock_grid_set(7, 14, true); /* v */
  int displacement_x = 8;
  int displacement_y = 7;
  move_down_right_probe_down_right(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 7);
  ASSERT_EQ(displacement_y, 0);
}

TEST(test_dr_probe_b1_hv_only) {
  mock_grid_clear();
  mock_grid_set(8, 14, true); /* hv at (65,116) T(8,14) */
  int displacement_x = 8;
  int displacement_y = 7;
  move_down_right_probe_down_right(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  /* For down-right: y_overlap > x_overlap → B1b → displacement_y clamped */
  ASSERT_EQ(displacement_y, 0);
  ASSERT_EQ(displacement_x, 8);
}

TEST(test_dr_probe_b2_hv_and_v) {
  mock_grid_clear();
  mock_grid_set(8, 14, true); /* hv */
  mock_grid_set(7, 14, true); /* v */
  int displacement_x = 8;
  int displacement_y = 7;
  move_down_right_probe_down_right(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_y, 0);
  ASSERT_EQ(displacement_x, 8);
}

TEST(test_dr_probe_c_hv_and_h) {
  mock_grid_clear();
  mock_grid_set(8, 14, true); /* hv */
  mock_grid_set(8, 13, true); /* h */
  int displacement_x = 8;
  int displacement_y = 7;
  move_down_right_probe_down_right(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 7);
  ASSERT_EQ(displacement_y, 7);
}

TEST(test_dr_probe_d_all_solid) {
  mock_grid_clear();
  mock_grid_set(8, 14, true);
  mock_grid_set(8, 13, true);
  mock_grid_set(7, 14, true);
  int displacement_x = 8;
  int displacement_y = 7;
  move_down_right_probe_down_right(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 7);
  ASSERT_EQ(displacement_y, 0);
}

/* ========================================================================
 * Additional: DOWN-LEFT corner probe branches
 * ======================================================================== */

TEST(test_dl_probe_a1b_v_only) {
  mock_grid_clear();
  mock_grid_set(5, 14, true); /* v at (42,118) T(5,14) off 6 */
  int displacement_x = -8;
  int displacement_y = 7;
  move_down_left_probe_down_left(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, -8);
  ASSERT_EQ(displacement_y, 0);
}

TEST(test_dl_probe_a2a_h_only) {
  mock_grid_clear();
  mock_grid_set(4, 13, true); /* h at (34,111) T(4,13) */
  int displacement_x = -8;
  int displacement_y = 7;
  move_down_left_probe_down_left(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, -2); /* -8 + (8-2) */
  ASSERT_EQ(displacement_y, 7);
}

TEST(test_dl_probe_b1_hv_only) {
  mock_grid_clear();
  mock_grid_set(4, 14, true); /* hv at (33,116) T(4,14) */
  int displacement_x = -8;
  int displacement_y = 7;
  move_down_left_probe_down_left(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  /* For down-left: y_overlap > |x_overlap| → B1b → displacement_y clamped */
  ASSERT_EQ(displacement_y, 0);
  ASSERT_EQ(displacement_x, -8);
}

TEST(test_dl_probe_c_hv_and_h) {
  mock_grid_clear();
  mock_grid_set(4, 14, true);
  mock_grid_set(4, 13, true);
  int displacement_x = -8;
  int displacement_y = 7;
  move_down_left_probe_down_left(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, -2);
  ASSERT_EQ(displacement_y, 7);
}

TEST(test_dl_probe_d_all_solid) {
  mock_grid_clear();
  mock_grid_set(4, 14, true);
  mock_grid_set(4, 13, true);
  mock_grid_set(5, 14, true);
  int displacement_x = -8;
  int displacement_y = 7;
  move_down_left_probe_down_left(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, -2);
  ASSERT_EQ(displacement_y, 0);
}

/* ========================================================================
 * Additional: middle probes (move_y_*_probes_*)
 * ======================================================================== */

TEST(test_middle_right_h_solid) {
  mock_grid_clear();
  /* Position B, displacement_x=8 → right middle probe at (65, 80) T(8,10). Solid → displacement_x
   * clamped. */
  mock_grid_set(8, 10, true);
  int displacement_x = 8;
  int displacement_y = -7;
  move_y_right_probes_right(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 7); /* 8 - 1 */
}

TEST(test_middle_right_hv_only_solid) {
  mock_grid_clear();
  /* h at (65,80) T(8,10) empty; hv at (65,73) T(8,9) solid → displacement_x via hv */
  mock_grid_set(8, 9, true);
  int displacement_x = 8;
  int displacement_y = -7;
  move_y_right_probes_right(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 7);
}

TEST(test_middle_left_h_solid) {
  mock_grid_clear();
  /* Position B, displacement_x=-8.  Left middle i=1: h at (34,80) T(4,10). */
  mock_grid_set(4, 10, true);
  int displacement_x = -8;
  int displacement_y = -7;
  move_y_left_probes_left(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, -2); /* -8 + (8-2) */
}

TEST(test_middle_left_hv_only_solid) {
  mock_grid_clear();
  /* h at (34,80) T(4,10) empty; hv at (34,73) T(4,9) solid */
  mock_grid_set(4, 9, true);
  int displacement_x = -8;
  int displacement_y = -7;
  move_y_left_probes_left(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, -2);
}

/* ========================================================================
 * Additional: secondary diagonal probes
 * ======================================================================== */

TEST(test_ur_secondary_up_left_hv_solid) {
  mock_grid_clear();
  /* move_up_right_probe_up_left: hv at (left_x+displacement_x, top_y+displacement_y) = (50,58)
   * T(6,7) */
  mock_grid_set(6, 7, true);
  int displacement_x = 8;
  int displacement_y = -7;
  move_up_right_probe_up_left(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_y, -1); /* -7 + (8-2) */
  ASSERT_EQ(displacement_x, 8);
}

TEST(test_ur_secondary_up_left_v_solid) {
  mock_grid_clear();
  /* v at (left_x, top_y+displacement_y) = (42,58) T(5,7). hv empty. */
  mock_grid_set(5, 7, true);
  int displacement_x = 8;
  int displacement_y = -7;
  move_up_right_probe_up_left(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  /* Calls col_definition_lookup with out-of-range H/N → no change */
  ASSERT_EQ(displacement_x, 8);
  ASSERT_EQ(displacement_y, -7);
}

TEST(test_ur_secondary_down_right_hv_solid) {
  mock_grid_clear();
  /* move_up_right_probe_down_right:
   * hv at (right_x+displacement_x, bottom_y+displacement_y) = (65, 104) T(8,13) */
  mock_grid_set(8, 13, true);
  int displacement_x = 8;
  int displacement_y = -7;
  move_up_right_probe_down_right(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 7); /* 8 - 1 */
}

TEST(test_ur_secondary_down_right_h_solid) {
  mock_grid_clear();
  /* h at (right_x+displacement_x, bottom_y) = (65,111) T(8,13). hv empty (use displacement_y=-8). */
  mock_grid_set(8, 13, true);
  int displacement_x = 8;
  int displacement_y = -8;
  move_up_right_probe_down_right(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  /* col_definition_lookup with out-of-range params → no change */
  ASSERT_EQ(displacement_x, 8);
  ASSERT_EQ(displacement_y, -8);
}

TEST(test_ul_secondary_up_right_hv_solid) {
  mock_grid_clear();
  /* move_up_left_probe_up_right: hv at (right_x+displacement_x, top_y+displacement_y)
   * right_x=57, displacement_x=-8: (49,58) T(6,7) */
  mock_grid_set(6, 7, true);
  int displacement_x = -8;
  int displacement_y = -7;
  move_up_left_probe_up_right(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_y, -1);
}

TEST(test_ul_secondary_up_right_v_solid) {
  mock_grid_clear();
  /* v at (right_x, top_y+displacement_y) = (57,58) T(7,7). hv empty. */
  mock_grid_set(7, 7, true);
  int displacement_x = -8;
  int displacement_y = -7;
  move_up_left_probe_up_right(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, -8);
  ASSERT_EQ(displacement_y, -7);
}

TEST(test_ul_secondary_down_left_hv_solid) {
  mock_grid_clear();
  /* move_up_left_probe_down_left: hv at (left_x+displacement_x, bottom_y+displacement_y)
   * left_x=42, displacement_x=-8: (34, 104) T(4,13) */
  mock_grid_set(4, 13, true);
  int displacement_x = -8;
  int displacement_y = -7;
  move_up_left_probe_down_left(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, -2); /* -8 + (8-2) */
}

TEST(test_ul_secondary_down_left_h_solid) {
  mock_grid_clear();
  /* h at (left_x+displacement_x, bottom_y) = (34,111) T(4,13). hv empty (use displacement_y=-8). */
  mock_grid_set(4, 13, true);
  int displacement_x = -8;
  int displacement_y = -8;
  move_up_left_probe_down_left(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, -8);
  ASSERT_EQ(displacement_y, -8);
}

TEST(test_dr_secondary_down_left_hv_solid) {
  mock_grid_clear();
  /* move_down_right_probe_down_left: hv at (left_x+displacement_x, bottom_y+displacement_y)
   * left_x=42, displacement_x=8, bottom=111, displacement_y=7: (50,118) T(6,14) */
  mock_grid_set(6, 14, true);
  int displacement_x = 8;
  int displacement_y = 7;
  move_down_right_probe_down_left(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_y, 0); /* 7 - (6 + 1) */
}

TEST(test_dr_secondary_down_left_v_solid) {
  mock_grid_clear();
  /* v at (left_x, bottom_y+displacement_y) = (42,118) T(5,14). hv empty. */
  mock_grid_set(5, 14, true);
  int displacement_x = 8;
  int displacement_y = 7;
  move_down_right_probe_down_left(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 8);
  ASSERT_EQ(displacement_y, 7);
}

TEST(test_dr_secondary_up_right_hv_solid) {
  mock_grid_clear();
  /* move_down_right_probe_up_right: hv at (right_x+displacement_x, top_y+displacement_y)
   * right_x=57, displacement_x=8, top_y=65, displacement_y=7: (65,72) T(8,9) */
  mock_grid_set(8, 9, true);
  int displacement_x = 8;
  int displacement_y = 7;
  move_down_right_probe_up_right(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 7);
}

TEST(test_dr_secondary_up_right_h_solid) {
  mock_grid_clear();
  /* h at (right_x+displacement_x, top_y) = (65,65) T(8,8). hv empty. */
  mock_grid_set(8, 8, true);
  int displacement_x = 8;
  int displacement_y = 7;
  move_down_right_probe_up_right(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 8);
  ASSERT_EQ(displacement_y, 7);
}

TEST(test_dl_secondary_down_right_hv_solid) {
  mock_grid_clear();
  /* move_down_left_probe_down_right: hv at (right_x+displacement_x, bottom_y+displacement_y)
   * right_x=57, displacement_x=-8, bottom=111, displacement_y=7: (49,118) T(6,14) */
  mock_grid_set(6, 14, true);
  int displacement_x = -8;
  int displacement_y = 7;
  move_down_left_probe_down_right(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_y, 0);
}

TEST(test_dl_secondary_down_right_v_solid) {
  mock_grid_clear();
  /* v at (right_x, bottom_y+displacement_y) = (57,118) T(7,14). hv empty. */
  mock_grid_set(7, 14, true);
  int displacement_x = -8;
  int displacement_y = 7;
  move_down_left_probe_down_right(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, -8);
  ASSERT_EQ(displacement_y, 7);
}

TEST(test_dl_secondary_up_left_hv_solid) {
  mock_grid_clear();
  /* move_down_left_probe_up_left: hv at (left_x+displacement_x, top_y+displacement_y)
   * left_x=42, displacement_x=-8, top_y=65, displacement_y=7: (34,72) T(4,9) */
  mock_grid_set(4, 9, true);
  int displacement_x = -8;
  int displacement_y = 7;
  move_down_left_probe_up_left(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, -2);
}

TEST(test_dl_secondary_up_left_h_solid) {
  mock_grid_clear();
  /* h at (left_x+displacement_x, top_y) = (34,65) T(4,8). hv empty. */
  mock_grid_set(4, 8, true);
  int displacement_x = -8;
  int displacement_y = 7;
  move_down_left_probe_up_left(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, -8);
  ASSERT_EQ(displacement_y, 7);
}

/* ========================================================================
 * Additional: dispatch_single_axis remaining branches
 * ======================================================================== */

TEST(test_dispatch_dx_negative) {
  /* up-left with ceiling → displacement_y zeroed, displacement_x<0 → move_left_probes */
  mock_grid_clear();
  /* Make ceiling tiles (row 7) for left probe area so move_up_left_probe
   * zeros displacement_y via B2 branch. */
  mock_grid_set(5, 7, true); /* v at (40,60)→T(5,7) */
  mock_grid_set(4, 7, true); /* hv at (36,60)→T(4,7) */

  int displacement_x = -4;
  int displacement_y = -4;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &displacement_x, &displacement_y);
  /* displacement_y zeroed, displacement_x<0 → dispatch → move_left_probes */
  ASSERT_EQ(displacement_y, 0);
  ASSERT_TRUE(displacement_x <= 0);
}

TEST(test_dispatch_dy_positive) {
  /* down-left with left wall → displacement_x zeroed, displacement_y>0 → move_down_probes */
  mock_grid_clear();
  mock_grid_set(T_LEFT_TX - 1, T_BOTTOM_TY, true);

  int displacement_x = -4;
  int displacement_y = 4;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 0);
  ASSERT_TRUE(displacement_y >= 0);
}

TEST(test_dispatch_both_zeroed) {
  /* Up-left where h and v both clamp their axes to 0 simultaneously.
   * Position: default. displacement_x=-4, displacement_y=-4.
   * h at (36,64) T(4,8) off(4,0): displacement_x += 8-4 = 0.
   * v at (40,60) T(5,7) off(0,4): displacement_y += 8-4 = 0.
   * Make T(4,7), T(4,8), T(5,7) solid → both zeroed. */
  mock_grid_clear();
  mock_grid_set(4, 7, true);
  mock_grid_set(4, 8, true);
  mock_grid_set(5, 7, true);

  int displacement_x = -4;
  int displacement_y = -4;
  resolve_collision(T_WORLD_X, T_SPRITE_X, T_SPRITE_Y, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 0);
  ASSERT_EQ(displacement_y, 0);
}

/* ========================================================================
 * Additional: full resolve_collision diagonal with mid-probe hits
 * ======================================================================== */

TEST(test_resolve_ur_mid_probe_zeros_dx) {
  mock_grid_clear();
  /* Position B. First corner probe all empty. Middle probe h at T(8,10) solid
   * → displacement_x clamped to 7. Then dispatch: both nonzero → continue. */
  mock_grid_set(8, 10, true);
  int displacement_x = 8;
  int displacement_y = -7;
  resolve_collision(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, 7);
}

TEST(test_resolve_ul_mid_probe_hit) {
  mock_grid_clear();
  /* Middle left probe at T(4,10) solid → displacement_x clamped. */
  mock_grid_set(4, 10, true);
  int displacement_x = -8;
  int displacement_y = -7;
  resolve_collision(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_x, -2);
}

TEST(test_resolve_dr_full_chain) {
  mock_grid_clear();
  /* Down-right: floor at row 14 → first probe v/hv hit → displacement_y clamped */
  mock_grid_fill((TileRect){0, 14, 20, 15});
  int displacement_x = 8;
  int displacement_y = 7;
  resolve_collision(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_y, 0);
}

TEST(test_resolve_dl_full_chain) {
  mock_grid_clear();
  /* Down-left: floor at row 14 */
  mock_grid_fill((TileRect){0, 14, 20, 15});
  int displacement_x = -8;
  int displacement_y = 7;
  resolve_collision(B_WX, B_SX, B_SY, &displacement_x, &displacement_y);
  ASSERT_EQ(displacement_y, 0);
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
  RUN_TEST(test_ur_probe_a1b_v_only);
  RUN_TEST(test_ur_probe_a2a_h_only);
  RUN_TEST(test_ur_probe_a2b_h_and_v);
  RUN_TEST(test_ur_probe_b1_hv_only);
  RUN_TEST(test_ur_probe_b2_hv_and_v);
  RUN_TEST(test_ur_probe_c_hv_and_h);
  RUN_TEST(test_ur_probe_d_all_solid);

  printf("\n[up-left corner probe branches]\n");
  RUN_TEST(test_ul_probe_a1b_v_only);
  RUN_TEST(test_ul_probe_a2a_h_only);
  RUN_TEST(test_ul_probe_a2b_h_and_v);
  RUN_TEST(test_ul_probe_b1_hv_only);
  RUN_TEST(test_ul_probe_b2_hv_and_v);
  RUN_TEST(test_ul_probe_c_hv_and_h);
  RUN_TEST(test_ul_probe_d_all_solid);

  printf("\n[down-right corner probe branches]\n");
  RUN_TEST(test_dr_probe_a1b_v_only);
  RUN_TEST(test_dr_probe_a2a_h_only);
  RUN_TEST(test_dr_probe_a2b_h_and_v);
  RUN_TEST(test_dr_probe_b1_hv_only);
  RUN_TEST(test_dr_probe_b2_hv_and_v);
  RUN_TEST(test_dr_probe_c_hv_and_h);
  RUN_TEST(test_dr_probe_d_all_solid);

  printf("\n[down-left corner probe branches]\n");
  RUN_TEST(test_dl_probe_a1b_v_only);
  RUN_TEST(test_dl_probe_a2a_h_only);
  RUN_TEST(test_dl_probe_b1_hv_only);
  RUN_TEST(test_dl_probe_c_hv_and_h);
  RUN_TEST(test_dl_probe_d_all_solid);

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
  if (tests_failed > 0) {
    printf(", %d FAILED", tests_failed);
  }
  printf("\n");

  return tests_failed > 0 ? 1 : 0;
}
