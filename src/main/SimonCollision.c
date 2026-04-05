#include "SimonCollision.h"

#include <stdio.h>

#include "LoadFile.h"
#include "Simon.h"
#include "Tilengine.h"

/* ---------------------------------------------------------------------------
 * col_definition lookup table
 *
 * col_thresh[H][N][row_idisplacement_x] = first valid (V) column for that grid row,
 * or COL_THRESH_NONE if the row has no valid destination.
 *
 * Indexing (up-right movement, probe at sprite-local (8,0)):
 *   H        1..8  — block rows overlapping the top of the player box
 *   N        1..9  — block width in pixel columns at the probe
 *   row_idisplacement_x  0..7  — maps to displacement_y as: row_idisplacement_x = 8 +
 * displacement_y (row 0 = displacement_y=-8 = topmost, row 7 = displacement_y=-1 = just above)
 *   col_idisplacement_x  0..8  — equals displacement_x (column 0 = probe's current x, column
 * displacement_x = destination)
 * ------------------------------------------------------------------------- */
#define COL_THRESH_NONE 9
#define COL_DEF_MAX_H 8
#define COL_DEF_MAX_N 9
#define COL_DEF_ROWS 8

static int col_thresh[COL_DEF_MAX_H + 1][COL_DEF_MAX_N + 1][COL_DEF_ROWS];

void load_col_definition(void) {
  for (int h = 0; h <= COL_DEF_MAX_H; h++) {
    for (int n = 0; n <= COL_DEF_MAX_N; n++) {
      for (int row = 0; row < COL_DEF_ROWS; row++) {
        col_thresh[h][n][row] = COL_THRESH_NONE;
      }
    }
  }

  FILE *file = FileOpen("col_definition");
  if (file == NULL) {
    return;
  }

  int cur_H = 0;
  int cur_N = 0;
  int cur_row = 0;
  char line[64];
  while (fgets(line, sizeof(line), file) != NULL) {
    int h;
    int n;
    if (sscanf(line, "# %dx%d", &h, &n) == 2) {
      cur_H = h;
      cur_N = n;
      cur_row = 0;
      continue;
    }
    if (cur_H < 1 || cur_H > COL_DEF_MAX_H || cur_N < 1 || cur_N > COL_DEF_MAX_N) {
      continue;
    }
    if (line[0] == '/' || line[0] == '\n' || line[0] == '\r') {
      continue;
    }
    if (line[0] == '*' || line[0] == 'P') {
      continue; /* skip the probe/player row */
    }
    if (cur_row >= COL_DEF_ROWS) {
      continue;
    }

    int thresh = COL_THRESH_NONE;
    char *p = line;
    for (int col = 0; col < 9 && thresh == COL_THRESH_NONE; col++) {
      while (*p == ' ') {
        p++;
      }
      if (*p == 'V') {
        thresh = col;
      }
      while (*p && *p != ',') {
        p++;
      }
      if (*p == ',') {
        p++;
      }
    }
    col_thresh[cur_H][cur_N][cur_row] = thresh;
    cur_row++;
  }
  fclose(file);
}

/*
 * Extended col_definition_lookup with rotation and mirror support.
 *
 * The col_definition grids encode the up-right (displacement_x>0, displacement_y<0) movement case.
 * This function rotates and/or mirrors the grid before the lookup so all
 * eight probe orientations can be handled with one table.
 *
 * rotations  0..3 — number of 90° clockwise rotations applied to the grid:
 *              0 = up-right   (original)    needs displacement_x>0, displacement_y<0
 *              1 = down-right              needs displacement_x>0, displacement_y>0
 *              2 = down-left               needs displacement_x<0, displacement_y>0
 *              3 = up-left                 needs displacement_x<0, displacement_y<0
 * mirror_h   flip the grid horizontally (reverses the displacement_x axis)
 * mirror_v   flip the grid vertically   (reverses the displacement_y axis)
 *
 * The combined transform is T = Mirror ∘ Rotate (rotate first, then mirror).
 * To query the transformed grid the displacement is brought back to the
 * original frame via T_inv = Rotate_inv ∘ Mirror, and lookups use the same
 * col_thresh table.  When the destination is invalid the clamped vector is
 * transformed back into the caller's frame before being written to *displacement_x /
 * *displacement_y.
 *
 * A single CW rotation step maps (u,v) → (−v, u) in screen coordinates.
 *
 * H, N: grid selector (same semantics as col_definition_lookup, interpreted
 * in the rotated frame).
 */
static void col_definition_lookup(int H, int N, int rotations, bool mirror_h, bool mirror_v,
                                  int *displacement_x, int *displacement_y) {
  if (H < 1 || H > COL_DEF_MAX_H || N < 1 || N > COL_DEF_MAX_N) {
    return;
  }

  /* Bring (displacement_x,displacement_y) into the original up-right frame.
   * T_inv = Rotate_inv ∘ Mirror; mirrors are self-inverse. */
  int velocity_x = *displacement_x;
  int velocity_y = *displacement_y;
  if (mirror_h) {
    velocity_x = -velocity_x;
  }
  if (mirror_v) {
    velocity_y = -velocity_y;
  }

  int inv_rot = (4 - (rotations % 4)) % 4;
  for (int i = 0; i < inv_rot; i++) { /* one CW step: (u,v) → (−v, u) */
    int tmp = velocity_x;
    velocity_x = -velocity_y;
    velocity_y = tmp;
  }

  /* Now (u,v) must be in the original up-right frame: u>0, v<0 */
  if (velocity_x <= 0 || velocity_y >= 0) {
    return;
  }

  int row_idisplacement_x =
      COL_DEF_ROWS + velocity_y; /* v = orig_displacement_y; row 0 = displacement_y=-8 .. row 7 =
                                    displacement_y=-1 */
  if (row_idisplacement_x < 0 || row_idisplacement_x >= COL_DEF_ROWS) {
    return;
  }

  if (velocity_x >= col_thresh[H][N][row_idisplacement_x]) {
    return; /* V cell — valid, no change */
  }

  /* Invalid: clamp orig_displacement_y to H-8, keeping orig_displacement_x.
   * Then apply the forward transform T = Mirror ∘ Rotate to write back. */
  int clamped_velocity_x = velocity_x;
  int clamped_velocity_y = H - MAX_VELOCITY;

  for (int i = 0; i < (rotations % 4); i++) { /* one CW step */
    int tmp = clamped_velocity_x;
    clamped_velocity_x = -clamped_velocity_y;
    clamped_velocity_y = tmp;
  }
  if (mirror_h) {
    clamped_velocity_x = -clamped_velocity_x;
  }
  if (mirror_v) {
    clamped_velocity_y = -clamped_velocity_y;
  }

  *displacement_x = clamped_velocity_x;
  *displacement_y = clamped_velocity_y;
}

/* ---------------------------------------------------------------------------
 * Probing
 * ------------------------------------------------------------------------- */

static void move_left_probes(int world_x, int sprite_x, int sprite_y, int *displacement_x,
                             bool (*probes)[4]) {
  for (int row = 0; row < 4; row++) {
    if (probes[0][row]) {
      continue;
    }
    TLN_TileInfo h_tile;
    TLN_GetLayerTile(COLLISION_LAYER, SIMON_COL_LEFT_X(world_x, sprite_x) + *displacement_x,
                     row == 0   ? sprite_y
                     : row == 3 ? sprite_y + (BLOCK_SIZE * row) - 3
                                : sprite_y + (BLOCK_SIZE * row) - 1,
                     &h_tile);

    if (!h_tile.empty) {
      *displacement_x += TILE_SIZE - h_tile.xoffset;
      return;
    }
  }
}

static void move_right_probes(int world_x, int sprite_x, int sprite_y, int *displacement_x,
                              bool (*probes)[4]) {
  for (int row = 0; row < 4; row++) {
    if (probes[1][row]) {
      continue;
    }
    TLN_TileInfo h_tile;
    TLN_GetLayerTile(COLLISION_LAYER, SIMON_COL_RIGHT_X(world_x, sprite_x) + *displacement_x,
                     row == 0   ? sprite_y
                     : row == 3 ? sprite_y + (BLOCK_SIZE * row) - 3
                                : sprite_y + (BLOCK_SIZE * row) - 1,
                     &h_tile);

    if (!h_tile.empty) {
      *displacement_x -= h_tile.xoffset;
      return;
    }
  }
}

static void move_up_probes(int world_x, int sprite_x, int sprite_y, int *displacement_y,
                           bool (*probes)[4]) {
  for (int column = 0; column < 2; column++) {
    if (probes[column][0]) {
      continue;
    }
    TLN_TileInfo v_tile;
    TLN_GetLayerTile(COLLISION_LAYER,
                     column == 0 ? SIMON_COL_LEFT_X(world_x, sprite_x)
                                 : SIMON_COL_LEFT_X(world_x, sprite_x) + (BLOCK_SIZE * column) - 1,
                     sprite_y + *displacement_y, &v_tile);

    if (!v_tile.empty) {
      *displacement_y += TILE_SIZE - v_tile.yoffset;
      return;
    }
  }
}

static void move_down_probes(int world_x, int sprite_x, int sprite_y, int *displacement_y,
                             bool (*probes)[4]) {
  for (int colummn = 0; colummn < 2; colummn++) {
    if (probes[colummn][3]) {
      continue;
    }
    TLN_TileInfo v_tile;
    TLN_GetLayerTile(COLLISION_LAYER,
                     colummn == 0
                         ? SIMON_COL_LEFT_X(world_x, sprite_x)
                         : SIMON_COL_LEFT_X(world_x, sprite_x) + (BLOCK_SIZE * colummn) - 1,
                     SIMON_COL_BOTTOM_Y(sprite_y) + *displacement_y, &v_tile);

    if (!v_tile.empty) {
      *displacement_y -= v_tile.yoffset;
      return;
    }
  }
}

static void move_up_right_probe_up_right(int world_x, int sprite_x, int sprite_y,
                                         int *displacement_x, int *displacement_y) {
  TLN_TileInfo h_tile;
  TLN_GetLayerTile(COLLISION_LAYER, SIMON_COL_RIGHT_X(world_x, sprite_x) + *displacement_x,
                   sprite_y, &h_tile);
  TLN_TileInfo v_tile;
  TLN_GetLayerTile(COLLISION_LAYER, SIMON_COL_RIGHT_X(world_x, sprite_x),
                   sprite_y + *displacement_y, &v_tile);
  TLN_TileInfo hv_tile;
  TLN_GetLayerTile(COLLISION_LAYER, SIMON_COL_RIGHT_X(world_x, sprite_x) + *displacement_x,
                   sprite_y + *displacement_y, &hv_tile);

  if (hv_tile.empty) {
    if (h_tile.empty) {
      if (v_tile.empty) {
        return;
      }
      *displacement_y += TILE_SIZE - v_tile.yoffset;
      return;
    }
    if (v_tile.empty) {
      *displacement_x -= h_tile.xoffset;
      return;
    }
    *displacement_x -= h_tile.xoffset;
    *displacement_y += TILE_SIZE - v_tile.yoffset;
    return;
  }
  if (h_tile.empty) {
    if (v_tile.empty) {
      int x_overlap = (SIMON_COL_RIGHT_X(world_x, sprite_x) + *displacement_x) - hv_tile.xoffset;
      int y_overlap = hv_tile.yoffset - (sprite_y + *displacement_y);
      if (y_overlap < x_overlap) {
        *displacement_x -= hv_tile.xoffset;
        return;
      }
      *displacement_y += TILE_SIZE - hv_tile.yoffset;
      return;
    }
    *displacement_y += TILE_SIZE - v_tile.yoffset;
    return;
  }
  if (v_tile.empty) {
    *displacement_x -= h_tile.xoffset;
    return;
  }
  *displacement_x -= h_tile.xoffset;
  *displacement_y += TILE_SIZE - v_tile.yoffset;
}

static void move_up_left_probe_up_left(int world_x, int sprite_x, int sprite_y, int *displacement_x,
                                       int *displacement_y) {
  TLN_TileInfo h_tile;
  TLN_GetLayerTile(COLLISION_LAYER, SIMON_COL_LEFT_X(world_x, sprite_x) + *displacement_x, sprite_y,
                   &h_tile);
  TLN_TileInfo v_tile;
  TLN_GetLayerTile(COLLISION_LAYER, SIMON_COL_LEFT_X(world_x, sprite_x), sprite_y + *displacement_y,
                   &v_tile);
  TLN_TileInfo hv_tile;
  TLN_GetLayerTile(COLLISION_LAYER, SIMON_COL_LEFT_X(world_x, sprite_x) + *displacement_x,
                   sprite_y + *displacement_y, &hv_tile);

  if (hv_tile.empty) {
    if (h_tile.empty) {
      if (v_tile.empty) {
        return;
      }
      *displacement_y += TILE_SIZE - v_tile.yoffset;
      return;
    }
    *displacement_x += TILE_SIZE - h_tile.xoffset;
    if (v_tile.empty) {
      return;
    }
    *displacement_y += TILE_SIZE - v_tile.yoffset;
    return;
  }
  if (h_tile.empty) {
    if (v_tile.empty) {
      int x_overlap =
          (hv_tile.xoffset + BLOCK_SIZE) - (SIMON_COL_LEFT_X(world_x, sprite_x) + *displacement_x);
      int y_overlap = hv_tile.yoffset - (sprite_y + *displacement_y);
      if (y_overlap < x_overlap) {
        *displacement_x += TILE_SIZE - hv_tile.xoffset;
        return;
      }
      *displacement_y += TILE_SIZE - hv_tile.yoffset;
      return;
    }
    *displacement_y += TILE_SIZE - v_tile.yoffset;
    return;
  }
  if (v_tile.empty) {
    *displacement_x += TILE_SIZE - h_tile.xoffset;
    return;
  }
  *displacement_x += TILE_SIZE - h_tile.xoffset;
  *displacement_y += TILE_SIZE - v_tile.yoffset;
}

static void move_down_right_probe_down_right(int world_x, int sprite_x, int sprite_y,
                                             int *displacement_x, int *displacement_y) {
  TLN_TileInfo h_tile;
  TLN_GetLayerTile(COLLISION_LAYER, SIMON_COL_RIGHT_X(world_x, sprite_x) + *displacement_x,
                   SIMON_COL_BOTTOM_Y(sprite_y), &h_tile);
  TLN_TileInfo v_tile;
  TLN_GetLayerTile(COLLISION_LAYER, SIMON_COL_RIGHT_X(world_x, sprite_x),
                   SIMON_COL_BOTTOM_Y(sprite_y) + *displacement_y, &v_tile);
  TLN_TileInfo hv_tile;
  TLN_GetLayerTile(COLLISION_LAYER, SIMON_COL_RIGHT_X(world_x, sprite_x) + *displacement_x,
                   SIMON_COL_BOTTOM_Y(sprite_y) + *displacement_y, &hv_tile);

  if (hv_tile.empty) {
    if (h_tile.empty) {
      if (v_tile.empty) {
        return;
      }
      *displacement_y -= v_tile.yoffset;
      return;
    }
    *displacement_x -= h_tile.xoffset;
    if (v_tile.empty) {
      return;
    }
    *displacement_y -= v_tile.yoffset;
    return;
  }
  if (h_tile.empty) {
    if (v_tile.empty) {
      int x_overlap = (SIMON_COL_RIGHT_X(world_x, sprite_x) + *displacement_x) - hv_tile.xoffset;
      int y_overlap = (SIMON_COL_BOTTOM_Y(sprite_y) + *displacement_y) - hv_tile.yoffset;
      if (y_overlap < x_overlap) {
        *displacement_x -= hv_tile.xoffset;
        return;
      }
      *displacement_y -= hv_tile.yoffset;
      return;
    }
    *displacement_y -= v_tile.yoffset;
    return;
  }
  if (v_tile.empty) {
    *displacement_x -= h_tile.xoffset;
    return;
  }
  *displacement_x -= h_tile.xoffset;
  *displacement_y -= v_tile.yoffset;
}

static void move_down_left_probe_down_left(int world_x, int sprite_x, int sprite_y,
                                           int *displacement_x, int *displacement_y) {
  TLN_TileInfo h_tile;
  TLN_GetLayerTile(COLLISION_LAYER, SIMON_COL_LEFT_X(world_x, sprite_x) + *displacement_x,
                   SIMON_COL_BOTTOM_Y(sprite_y), &h_tile);
  TLN_TileInfo v_tile;
  TLN_GetLayerTile(COLLISION_LAYER, SIMON_COL_LEFT_X(world_x, sprite_x),
                   SIMON_COL_BOTTOM_Y(sprite_y) + *displacement_y, &v_tile);
  TLN_TileInfo hv_tile;
  TLN_GetLayerTile(COLLISION_LAYER, SIMON_COL_LEFT_X(world_x, sprite_x) + *displacement_x,
                   SIMON_COL_BOTTOM_Y(sprite_y) + *displacement_y, &hv_tile);

  if (hv_tile.empty) {
    if (h_tile.empty) {
      if (v_tile.empty) {
        return;
      }
      *displacement_y -= v_tile.yoffset;
      return;
    }
    *displacement_x += TILE_SIZE - h_tile.xoffset;
    if (v_tile.empty) {
      return;
    }
    *displacement_y -= v_tile.yoffset;
    return;
  }
  if (h_tile.empty) {
    if (v_tile.empty) {
      int x_overlap =
          (hv_tile.xoffset + BLOCK_SIZE) - (SIMON_COL_LEFT_X(world_x, sprite_x) + *displacement_x);
      int y_overlap = (SIMON_COL_BOTTOM_Y(sprite_y) + *displacement_y) - hv_tile.yoffset;
      if (y_overlap < x_overlap) {
        *displacement_x += TILE_SIZE - hv_tile.xoffset;
        return;
      }
      *displacement_y -= hv_tile.yoffset;
      return;
    }
    *displacement_y -= v_tile.yoffset;
    return;
  }
  *displacement_x += TILE_SIZE - h_tile.xoffset;
  if (v_tile.empty) {
    return;
  }
  *displacement_y -= v_tile.yoffset;
}

static void move_up_right_probe_up_left(int world_x, int sprite_x, int sprite_y,
                                        int *displacement_x, int *displacement_y) {
  TLN_TileInfo v_tile;
  TLN_GetLayerTile(COLLISION_LAYER, SIMON_COL_LEFT_X(world_x, sprite_x), sprite_y + *displacement_y,
                   &v_tile);
  TLN_TileInfo hv_tile;
  TLN_GetLayerTile(COLLISION_LAYER, SIMON_COL_LEFT_X(world_x, sprite_x) + *displacement_x,
                   sprite_y + *displacement_y, &hv_tile);

  if (hv_tile.empty) {
    if (v_tile.empty) {
      return;
    }
    int h = MAX_VELOCITY - (sprite_y - (v_tile.yoffset + BLOCK_SIZE));
    int n = v_tile.xoffset - (SIMON_COL_LEFT_X(world_x, sprite_x));

    col_definition_lookup(h, n, 0, false, false, displacement_x, displacement_y);
    return;
  }
  *displacement_y += TILE_SIZE - hv_tile.yoffset;
}

static void move_up_left_probe_up_right(int world_x, int sprite_x, int sprite_y,
                                        int *displacement_x, int *displacement_y) {
  TLN_TileInfo v_tile;
  TLN_GetLayerTile(COLLISION_LAYER, SIMON_COL_RIGHT_X(world_x, sprite_x),
                   sprite_y + *displacement_y, &v_tile);
  TLN_TileInfo hv_tile;
  TLN_GetLayerTile(COLLISION_LAYER, SIMON_COL_RIGHT_X(world_x, sprite_x) + *displacement_x,
                   sprite_y + *displacement_y, &hv_tile);

  if (hv_tile.empty) {
    if (v_tile.empty) {
      return;
    }
    int h = TILE_SIZE - (sprite_y - (v_tile.yoffset + BLOCK_SIZE));
    int n = v_tile.xoffset - (SIMON_COL_RIGHT_X(world_x, sprite_x));

    col_definition_lookup(h, n, 0, true, false, displacement_x, displacement_y);
    return;
  }
  *displacement_y += TILE_SIZE - hv_tile.yoffset;
}

static void move_up_right_probe_down_right(int world_x, int sprite_x, int sprite_y,
                                           int *displacement_x, int *displacement_y) {
  TLN_TileInfo h_tile;
  TLN_GetLayerTile(COLLISION_LAYER, SIMON_COL_RIGHT_X(world_x, sprite_x) + *displacement_x,
                   SIMON_COL_BOTTOM_Y(sprite_y), &h_tile);
  TLN_TileInfo hv_tile;
  TLN_GetLayerTile(COLLISION_LAYER, SIMON_COL_RIGHT_X(world_x, sprite_x) + *displacement_x,
                   SIMON_COL_BOTTOM_Y(sprite_y) + *displacement_y, &hv_tile);

  if (hv_tile.empty) {
    if (h_tile.empty) {
      return;
    }
    int h = MAX_VELOCITY - (SIMON_COL_RIGHT_X(world_x, sprite_x) - h_tile.xoffset);
    int n = h_tile.yoffset - sprite_y;

    col_definition_lookup(h, n, 1, false, true, displacement_x, displacement_y);
    return;
  }
  *displacement_x -= hv_tile.xoffset;
}

static void move_y_left_probes_left(int world_x, int sprite_x, int sprite_y, int *displacement_x,
                                    const int *displacement_y) {
  for (int i = 1; i < 3; i++) {
    TLN_TileInfo h_tile;
    TLN_GetLayerTile(COLLISION_LAYER, SIMON_COL_LEFT_X(world_x, sprite_x) + *displacement_x,
                     sprite_y + (BLOCK_SIZE * i) - 1, &h_tile);
    TLN_TileInfo hv_tile;
    TLN_GetLayerTile(COLLISION_LAYER, SIMON_COL_LEFT_X(world_x, sprite_x) + *displacement_x,
                     sprite_y + (BLOCK_SIZE * i) - 1 + *displacement_y, &hv_tile);

    if ((int)hv_tile.empty && (int)h_tile.empty) {
      continue;
    }

    if (!h_tile.empty) {
      *displacement_x += TILE_SIZE - h_tile.xoffset;
    } else if (!hv_tile.empty) {
      *displacement_x += TILE_SIZE - hv_tile.xoffset;
    }

    return;
  }
}

static void move_y_right_probes_right(int world_x, int sprite_x, int sprite_y, int *displacement_x,
                                      const int *displacement_y) {
  for (int i = 1; i < 3; i++) {
    TLN_TileInfo h_tile;
    TLN_GetLayerTile(COLLISION_LAYER, SIMON_COL_RIGHT_X(world_x, sprite_x) + *displacement_x,
                     sprite_y + (BLOCK_SIZE * i) - 1, &h_tile);
    TLN_TileInfo hv_tile;
    TLN_GetLayerTile(COLLISION_LAYER, SIMON_COL_RIGHT_X(world_x, sprite_x) + *displacement_x,
                     sprite_y + (BLOCK_SIZE * i) - 1 + *displacement_y, &hv_tile);

    if ((int)hv_tile.empty && (int)h_tile.empty) {
      continue;
    }

    if (!h_tile.empty) {
      *displacement_x -= h_tile.xoffset;
    } else if (!hv_tile.empty) {
      *displacement_x -= hv_tile.xoffset;
    }

    return;
  }
}

static void move_up_left_probe_down_left(int world_x, int sprite_x, int sprite_y,
                                         int *displacement_x, int *displacement_y) {
  TLN_TileInfo h_tile;
  TLN_GetLayerTile(COLLISION_LAYER, SIMON_COL_LEFT_X(world_x, sprite_x) + *displacement_x,
                   SIMON_COL_BOTTOM_Y(sprite_y), &h_tile);
  TLN_TileInfo hv_tile;
  TLN_GetLayerTile(COLLISION_LAYER, SIMON_COL_LEFT_X(world_x, sprite_x) + *displacement_x,
                   SIMON_COL_BOTTOM_Y(sprite_y) + *displacement_y, &hv_tile);

  if (hv_tile.empty) {
    if (h_tile.empty) {
      return;
    }
    int h = TILE_SIZE - (SIMON_COL_LEFT_X(world_x, sprite_x) - h_tile.xoffset);
    int n = h_tile.yoffset - sprite_y;

    col_definition_lookup(h, n, 1, true, true, displacement_x, displacement_y);
    return;
  }
  *displacement_x += TILE_SIZE - hv_tile.xoffset;
}

static void move_down_right_probe_down_left(int world_x, int sprite_x, int sprite_y,
                                            int *displacement_x, int *displacement_y) {
  TLN_TileInfo v_tile;
  TLN_GetLayerTile(COLLISION_LAYER, SIMON_COL_LEFT_X(world_x, sprite_x),
                   SIMON_COL_BOTTOM_Y(sprite_y) + *displacement_y, &v_tile);
  TLN_TileInfo hv_tile;
  TLN_GetLayerTile(COLLISION_LAYER, SIMON_COL_LEFT_X(world_x, sprite_x) + *displacement_x,
                   SIMON_COL_BOTTOM_Y(sprite_y) + *displacement_y, &hv_tile);

  if (hv_tile.empty) {
    if (v_tile.empty) {
      return;
    }
    int h = TILE_SIZE - ((SIMON_COL_BOTTOM_Y(sprite_y)) - v_tile.yoffset);
    int n = v_tile.xoffset - (SIMON_COL_LEFT_X(world_x, sprite_x));

    col_definition_lookup(h, n, 0, false, true, displacement_x, displacement_y);
    return;
  }
  *displacement_y -= hv_tile.yoffset;
}

static void move_down_right_probe_up_right(int world_x, int sprite_x, int sprite_y,
                                           int *displacement_x, int *displacement_y) {
  TLN_TileInfo h_tile;
  TLN_GetLayerTile(COLLISION_LAYER, SIMON_COL_RIGHT_X(world_x, sprite_x) + *displacement_x,
                   sprite_y, &h_tile);
  TLN_TileInfo hv_tile;
  TLN_GetLayerTile(COLLISION_LAYER, SIMON_COL_RIGHT_X(world_x, sprite_x) + *displacement_x,
                   sprite_y + *displacement_y, &hv_tile);

  if (hv_tile.empty) {
    if (h_tile.empty) {
      return;
    }
    int h = TILE_SIZE - (SIMON_COL_RIGHT_X(world_x, sprite_x) - h_tile.xoffset);
    int n = h_tile.yoffset + BLOCK_SIZE - sprite_y;

    col_definition_lookup(h, n, 1, false, false, displacement_x, displacement_y);
    return;
  }
  *displacement_x -= hv_tile.xoffset;
}

static void move_down_left_probe_down_right(int world_x, int sprite_x, int sprite_y,
                                            int *displacement_x, int *displacement_y) {
  TLN_TileInfo v_tile;
  TLN_GetLayerTile(COLLISION_LAYER, SIMON_COL_RIGHT_X(world_x, sprite_x),
                   SIMON_COL_BOTTOM_Y(sprite_y) + *displacement_y, &v_tile);
  TLN_TileInfo hv_tile;
  TLN_GetLayerTile(COLLISION_LAYER, SIMON_COL_RIGHT_X(world_x, sprite_x) + *displacement_x,
                   SIMON_COL_BOTTOM_Y(sprite_y) + *displacement_y, &hv_tile);

  if (hv_tile.empty) {
    if (v_tile.empty) {
      return;
    }
    int h = TILE_SIZE - ((SIMON_COL_BOTTOM_Y(sprite_y)) - v_tile.yoffset);
    int n = v_tile.xoffset - (SIMON_COL_RIGHT_X(world_x, sprite_x));

    col_definition_lookup(h, n, 0, true, true, displacement_x, displacement_y);
    return;
  }
  *displacement_y -= hv_tile.yoffset;
}

static void move_down_left_probe_up_left(int world_x, int sprite_x, int sprite_y,
                                         int *displacement_x, int *displacement_y) {
  TLN_TileInfo h_tile;
  TLN_GetLayerTile(COLLISION_LAYER, SIMON_COL_LEFT_X(world_x, sprite_x) + *displacement_x, sprite_y,
                   &h_tile);
  TLN_TileInfo hv_tile;
  TLN_GetLayerTile(COLLISION_LAYER, SIMON_COL_LEFT_X(world_x, sprite_x) + *displacement_x,
                   sprite_y + *displacement_y, &hv_tile);

  if (hv_tile.empty) {
    if (h_tile.empty) {
      return;
    }
    int h = TILE_SIZE - (SIMON_COL_LEFT_X(world_x, sprite_x) - h_tile.xoffset);
    int n = h_tile.yoffset + BLOCK_SIZE - sprite_y;

    col_definition_lookup(h, n, 1, true, false, displacement_x, displacement_y);
    return;
  }
  *displacement_x += TILE_SIZE - hv_tile.xoffset;
}

/* After each diagonal probe, dispatch to the appropriate single-axis probe
 * set when one axis has been zeroed by collision.  Returns true if the
 * caller should stop (one or both axes are done). */
static bool dispatch_single_axis(int world_x, int sprite_x, int sprite_y, int *displacement_x,
                                 int *displacement_y, bool (*probes)[4]) {
  if (*displacement_x != 0 && *displacement_y != 0) {
    return false;
  }
  if (*displacement_y < 0) {
    move_up_probes(world_x, sprite_x, sprite_y, displacement_y, probes);
  } else if (*displacement_y > 0) {
    move_down_probes(world_x, sprite_x, sprite_y, displacement_y, probes);
  } else if (*displacement_x < 0) {
    move_left_probes(world_x, sprite_x, sprite_y, displacement_x, probes);
  } else if (*displacement_x > 0) {
    move_right_probes(world_x, sprite_x, sprite_y, displacement_x, probes);
  }
  return true;
}

static void move_up_right_probes(int world_x, int sprite_x, int sprite_y, int *displacement_x,
                                 int *displacement_y, bool (*probes)[4]) {
  move_up_right_probe_up_right(world_x, sprite_x, sprite_y, displacement_x, displacement_y);
  probes[1][0] = true;
  if (dispatch_single_axis(world_x, sprite_x, sprite_y, displacement_x, displacement_y, probes)) {
    return;
  }

  move_y_right_probes_right(world_x, sprite_x, sprite_y, displacement_x, displacement_y);
  probes[1][1] = true;
  probes[1][2] = true;
  if (dispatch_single_axis(world_x, sprite_x, sprite_y, displacement_x, displacement_y, probes)) {
    return;
  }

  move_up_right_probe_up_left(world_x, sprite_x, sprite_y, displacement_x, displacement_y);
  probes[0][0] = true;
  if (dispatch_single_axis(world_x, sprite_x, sprite_y, displacement_x, displacement_y, probes)) {
    return;
  }

  move_up_right_probe_down_right(world_x, sprite_x, sprite_y, displacement_x, displacement_y);
  probes[1][3] = true;
}

static void move_up_left_probes(int world_x, int sprite_x, int sprite_y, int *displacement_x,
                                int *displacement_y, bool (*probes)[4]) {
  move_up_left_probe_up_left(world_x, sprite_x, sprite_y, displacement_x, displacement_y);
  probes[0][0] = true;
  if (dispatch_single_axis(world_x, sprite_x, sprite_y, displacement_x, displacement_y, probes)) {
    return;
  }

  move_y_left_probes_left(world_x, sprite_x, sprite_y, displacement_x, displacement_y);
  probes[0][1] = true;
  probes[0][2] = true;
  if (dispatch_single_axis(world_x, sprite_x, sprite_y, displacement_x, displacement_y, probes)) {
    return;
  }

  move_up_left_probe_up_right(world_x, sprite_x, sprite_y, displacement_x, displacement_y);
  probes[1][0] = true;
  if (dispatch_single_axis(world_x, sprite_x, sprite_y, displacement_x, displacement_y, probes)) {
    return;
  }

  move_up_left_probe_down_left(world_x, sprite_x, sprite_y, displacement_x, displacement_y);
  probes[1][3] = true;
}

static void move_down_right_probes(int world_x, int sprite_x, int sprite_y, int *displacement_x,
                                   int *displacement_y, bool (*probes)[4]) {
  move_down_right_probe_down_right(world_x, sprite_x, sprite_y, displacement_x, displacement_y);
  probes[1][3] = true;
  if (dispatch_single_axis(world_x, sprite_x, sprite_y, displacement_x, displacement_y, probes)) {
    return;
  }

  move_y_right_probes_right(world_x, sprite_x, sprite_y, displacement_x, displacement_y);
  probes[1][1] = true;
  probes[1][2] = true;
  if (dispatch_single_axis(world_x, sprite_x, sprite_y, displacement_x, displacement_y, probes)) {
    return;
  }

  move_down_right_probe_down_left(world_x, sprite_x, sprite_y, displacement_x, displacement_y);
  probes[0][3] = true;
  if (dispatch_single_axis(world_x, sprite_x, sprite_y, displacement_x, displacement_y, probes)) {
    return;
  }

  move_down_right_probe_up_right(world_x, sprite_x, sprite_y, displacement_x, displacement_y);
  probes[1][0] = true;
}

static void move_down_left_probes(int world_x, int sprite_x, int sprite_y, int *displacement_x,
                                  int *displacement_y, bool (*probes)[4]) {
  move_down_left_probe_down_left(world_x, sprite_x, sprite_y, displacement_x, displacement_y);
  probes[0][3] = true;
  if (dispatch_single_axis(world_x, sprite_x, sprite_y, displacement_x, displacement_y, probes)) {
    return;
  }

  move_y_left_probes_left(world_x, sprite_x, sprite_y, displacement_x, displacement_y);
  probes[0][1] = true;
  probes[0][2] = true;
  if (dispatch_single_axis(world_x, sprite_x, sprite_y, displacement_x, displacement_y, probes)) {
    return;
  }

  move_down_left_probe_down_right(world_x, sprite_x, sprite_y, displacement_x, displacement_y);
  probes[1][3] = true;
  if (dispatch_single_axis(world_x, sprite_x, sprite_y, displacement_x, displacement_y, probes)) {
    return;
  }

  move_down_left_probe_up_left(world_x, sprite_x, sprite_y, displacement_x, displacement_y);
  probes[0][0] = true;
}

/*
 * Resolves a candidate displacement (*displacement_x, *displacement_y) against tile collision.
 *
 * Selects the probe set for the displacement direction, then for each probe
 * checks whether the destination pixel would be inside a solid tile.  Uses
 * single-axis retests to determine which axis caused the collision, and
 * clamps only that axis to stop just outside the tile boundary.  When
 * neither axis alone would enter the tile (a true diagonal corner), both
 * axes are clamped.  The result is the most restrictive valid displacement
 * across all five probes.
 *
 * Maximum displacement in either axis is 8 pixels (one frame at top speed);
 * tiles are 16 pixels wide, so at most one tile boundary can be crossed per
 * frame and the single-pass loop is sufficient.
 *
 * \param world_x   Horizontal scroll offset (camera position)
 * \param sprite_x  Sprite screen-x position
 * \param sprite_y  Sprite screen-y position (top of bounding box)
 * \param displacement_x        In/out: candidate horizontal displacement, clamped on return
 * \param displacement_y        In/out: candidate vertical displacement, clamped on return
 */
void resolve_collision(int world_x, int sprite_x, int sprite_y, int *displacement_x,
                       int *displacement_y) {
  bool probes[2][4] = {{false}};

  if (*displacement_x < 0) {
    if (*displacement_y < 0) {
      move_up_left_probes(world_x, sprite_x, sprite_y, displacement_x, displacement_y, probes);
    } else if (*displacement_y > 0) {
      move_down_left_probes(world_x, sprite_x, sprite_y, displacement_x, displacement_y, probes);
    } else {
      move_left_probes(world_x, sprite_x, sprite_y, displacement_x, probes);
    }
  } else if (*displacement_x > 0) {
    if (*displacement_y < 0) {
      move_up_right_probes(world_x, sprite_x, sprite_y, displacement_x, displacement_y, probes);
    } else if (*displacement_y > 0) {
      move_down_right_probes(world_x, sprite_x, sprite_y, displacement_x, displacement_y, probes);
    } else {
      move_right_probes(world_x, sprite_x, sprite_y, displacement_x, probes);
    }
  } else {
    if (*displacement_y < 0) {
      move_up_probes(world_x, sprite_x, sprite_y, displacement_y, probes);
    } else if (*displacement_y > 0) {
      move_down_probes(world_x, sprite_x, sprite_y, displacement_y, probes);
    }
  }
}
