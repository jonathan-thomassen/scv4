#ifndef GRID_SPRITESET_H
#define GRID_SPRITESET_H

#include <stdbool.h>

#include "Tilengine.h"

/**
 * Loads a spriteset from a compact grid-format txt file + matching PNG.
 *
 * Given base_name "foo", opens "foo.txt" for tile dimensions and "foo.png"
 * for the bitmap.  The txt file contains three key = value lines:
 *   w    = <tile width>
 *   h    = <tile height>
 *   cols = <number of columns>
 * Tile rows are derived from the bitmap height.  Sprites are named
 * s0, s1, ... sN in row-major order.
 *
 * The created bitmap is written to *out_bitmap.  The caller must delete
 * it after the spriteset is freed.
 */
TLN_Spriteset load_grid_spriteset(const char* base_name, TLN_Bitmap* out_bitmap);

/* ---------------------------------------------------------------------------
 * Map-section parser: shared by Simon and Whip sprite-map loaders.
 *
 * File format (shared by simon_map.txt, whip0_map.txt, etc.):
 *   # name                  <- named group header
 *   N:                      <- stage index (0-based)
 *   sP = ( dx, dy ) [flags] <- subsprite: P=picture, optional h/v flip flags
 * ------------------------------------------------------------------------- */

#define MAP_MAX_STAGES 8  /* max animation stages per section */
#define MAP_MAX_SEGS   12 /* max subsprites per stage         */

typedef struct {
  int pic;     /* picture index in the spriteset */
  int dx;      /* x offset from anchor           */
  int dy;      /* y offset from anchor           */
  bool flip_h; /* horizontal flip                */
  bool flip_v; /* vertical flip                  */
} MapSeg;

typedef struct {
  MapSeg segs[MAP_MAX_SEGS];
  int count;
} MapStage;

typedef struct {
  MapStage stages[MAP_MAX_STAGES];
  int num_stages; /* 1 + highest stage index parsed */
} MapSection;

/**
 * Parses a single named group from a map file into *out.
 *
 * max_stages and max_segs clamp the parser to the caller's array limits
 * (must not exceed MAP_MAX_STAGES / MAP_MAX_SEGS).
 */
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void load_map_section(const char* filename, const char* section, int max_stages, int max_segs, MapSection* out);

#endif /* GRID_SPRITESET_H */
