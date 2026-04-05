#include "Whip.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "LoadFile.h" /* FileOpen() – honours TLN_SetLoadPath()  */
#include "Simon.h"    /* SimonGetScreenX/Y, SimonFacingRight      */
#include "Tilengine.h"

#define SEG_SIZE 8        /* each subsprite is 8x8 px                     */
#define SIMON_SPRITE_W 32 /* width of Simon's sprite in pixels             */
#define NUM_STAGES 3      /* number of whip animation stages               */

/*
 * Loads a spriteset from a compact grid-format txt file + matching PNG.
 *
 * The txt file contains three key = value lines in any order:
 *   w    = <tile width>
 *   h    = <tile height>
 *   cols = <number of columns>
 * Tile rows are derived from the bitmap height.  Sprites are named
 * s0, s1, … sN in row-major order.
 */
static TLN_Spriteset load_grid_spriteset(const char *txt_name, const char *png_name,
                                         TLN_Bitmap *out_bitmap) {
  FILE *file = FileOpen(txt_name);
  if (file == NULL) {
    return NULL;
  }

  int tile_width = 0;
  int tile_height = 0;
  int cols = 0;
  char line[64];
  while (fgets(line, sizeof(line), file) != NULL) {
    int v;
    if (sscanf(line, " w = %d", &v) == 1) {
      tile_width = v;
    } else if (sscanf(line, " h = %d", &v) == 1) {
      tile_height = v;
    } else if (sscanf(line, " cols = %d", &v) == 1) {
      cols = v;
    }
  }
  fclose(file);

  if (tile_width <= 0 || tile_height <= 0 || cols <= 0) {
    return NULL;
  }

  TLN_Bitmap bmp = TLN_LoadBitmap(png_name);
  if (bmp == NULL) {
    return NULL;
  }

  int rows = TLN_GetBitmapHeight(bmp) / tile_height;
  int total = rows * cols;

  TLN_SpriteData *data = (TLN_SpriteData *)malloc((size_t)total * sizeof(TLN_SpriteData));
  if (data == NULL) {
    TLN_DeleteBitmap(bmp);
    return NULL;
  }

  for (int row = 0; row < rows; row++) {
    for (int column = 0; column < cols; column++) {
      TLN_SpriteData *sprite_data = &data[(row * cols) + column];
      snprintf(sprite_data->name, sizeof(sprite_data->name), "s%d", (row * cols) + column);
      sprite_data->x = column * tile_width;
      sprite_data->y = row * tile_height;
      sprite_data->w = tile_width;
      sprite_data->h = tile_height;
    }
  }

  TLN_Spriteset spriteset = TLN_CreateSpriteset(bmp, data, total);
  free(data);
  /* Do NOT delete bmp here — the spriteset holds a reference to it.
   * The caller is responsible for deleting it after the spriteset is freed. */
  *out_bitmap = bmp;
  return spriteset;
}

/*
 * Number of frames each animation stage is shown.
 * The sum of all entries equals the total visible window; the remaining
 * (WHIP_DURATION - sum) frames are a brief invisible retract pause before
 * control returns to Simon.  Add or change values here to adjust timing.
 */
static const int stage_durations[NUM_STAGES] = {
    5, 5, 13}; /* 5 frames for stage 0, 5 frames for stage 1, 13 frames for stage 2 */

/* Returns the total number of frames across all stages. */
static int total_visible_frames(void) {
  int total = 0;
  for (int i = 0; i < NUM_STAGES; i++) {
    total += stage_durations[i];
  }
  return total;
}

/* Returns the stage index for the given frame, based on stage_durations. */
static int frame_to_stage(int frame) {
  for (int i = 0; i < NUM_STAGES - 1; i++) {
    if (frame < stage_durations[i]) {
      return i;
    }
    frame -= stage_durations[i];
  }
  return NUM_STAGES - 1;
}

/*
 * Subsprite descriptor loaded from whip0_mapN.txt.
 * Positions are defined for Simon facing RIGHT (the default sprite orientation).
 * When Simon faces left, dx is mirrored as (SIMON_SPRITE_W - dx - SEG_SIZE)
 * and flip_h is inverted.
 */
typedef struct {
  int pic;     /* picture index in the spriteset (0-11)        */
  int dx;      /* x offset from Simon's top-left (left-facing) */
  int dy;      /* y offset from Simon's top-left               */
  bool flip_h; /* sprite's own horizontal flip                 */
  bool flip_v; /* sprite's own vertical flip                   */
} WhipSeg;

/* Runtime stage data: segments loaded from whip0_map.txt. */
typedef struct {
  WhipSeg segs[MAX_WHIP_SPRITES];
  int count; /* number of valid entries (0 = stage not loaded) */
} WhipStage;

static WhipStage stages[NUM_STAGES];    /* horizontal swing (# main) */
static WhipStage up_stages[NUM_STAGES]; /* upward swing    (# up)   */

static TLN_Spriteset spriteset;
static TLN_Bitmap spriteset_bmp;

/* Current frame within the swing; >= WHIP_DURATION means inactive. */
static int swing_frame;

/* Stage index that was rendered on the current game frame. WhipGetStage()
 * returns this so Simon's body is always in lock-step with the whip sprites,
 * regardless of where swing_frame++ lands after the draw. */
static int last_rendered_stage;

/* Facing direction frozen at the moment the swing was triggered. */
static bool swing_facing_right;

/* True when the current swing was triggered with INPUT_UP held. */
static bool swing_up;

/* Edge-detect state: prevents re-triggering while INPUT_B is held. */
static bool prev_pressed;

/* True on frames where visible whip sprites should be placed by WhipRender(). */
static bool render_pending;

static void disable_all_segments(void) {
  for (int i = 0; i < MAX_WHIP_SPRITES; i++) {
    TLN_DisableSprite(WHIP_SPRITE_BASE + i);
  }
}

/*
 * Parses a single map file into stages[], reading only the named group that
 * matches 'section'.
 *
 * File format:
 *   # name                 <- named group header; selects which group to load
 *   N:                     <- stage index header (0-based) within a group
 *   sP = ( dx, dy) [flags] <- subsprite line: P=picture index, dx/dy=offsets,
 *                             optional flags 'h' (flip-X) and/or 'v' (flip-Y)
 *
 * Lines starting with '#' that do not match the requested section cause all
 * subsequent stage lines to be ignored until the matching group is found.
 * Blank lines and unrecognised lines are silently skipped.
 */
static void load_map_file(const char *filename, const char *section, WhipStage *dest) {
  FILE *file = FileOpen(filename);
  if (file == NULL) {
    return;
  }

  bool in_section = false;
  int cur_stage = -1;
  char line[128];
  while (fgets(line, sizeof(line), file) != NULL) {
    /* Named group header: "# name" */
    if (line[0] == '#') {
      char name[64] = "";
      sscanf(line, "# %63s", name);
      in_section = (strcmp(name, section) == 0);
      cur_stage = -1; /* reset stage on every group boundary */
      continue;
    }

    if (!in_section) {
      continue;
    }

    /* Stage index header: "N:" */
    int idx = -1;
    if (sscanf(line, " %d :", &idx) == 1 && idx >= 0 && idx < NUM_STAGES) {
      cur_stage = idx;
      continue;
    }

    if (cur_stage < 0 || dest[cur_stage].count >= MAX_WHIP_SPRITES) {
      continue;
    }

    int pic = -1;
    int displacement_x = 0;
    int displacement_y = 0;
    char flags[8] = "";

    int matched =
        sscanf(line, " s%d = ( %d , %d ) %7s", &pic, &displacement_x, &displacement_y, flags);
    if (matched < 3 || pic < 0) {
      continue;
    }

    WhipSeg *segment = &dest[cur_stage].segs[dest[cur_stage].count];
    segment->pic = pic;
    segment->dx = displacement_x;
    segment->dy = displacement_y;
    segment->flip_h = (strchr(flags, 'h') != NULL);
    segment->flip_v = (strchr(flags, 'v') != NULL);
    dest[cur_stage].count++;
  }

  fclose(file);
}

void WhipInit(void) {
  spriteset = load_grid_spriteset("whip0.txt", "whip0.png", &spriteset_bmp);
  swing_frame = WHIP_DURATION; /* inactive */
  swing_up = false;
  prev_pressed = false;
  disable_all_segments();

  /* Load both swing directions from a single file. */
  load_map_file("whip0_map.txt", "main", stages);
  load_map_file("whip0_map.txt", "up", up_stages);
}

void WhipDeinit(void) {
  disable_all_segments();
  if (spriteset != NULL) {
    TLN_DeleteSpriteset(spriteset);
    spriteset = NULL;
  }
  if (spriteset_bmp != NULL) {
    TLN_DeleteBitmap(spriteset_bmp);
    spriteset_bmp = NULL;
  }
  for (int i = 0; i < NUM_STAGES; i++) {
    stages[i].count = 0;
  }
}

bool WhipIsActive(void) { return swing_frame < WHIP_DURATION; }

int WhipGetStage(void) {
  if (!WhipIsActive()) {
    return 0;
  }
  return last_rendered_stage;
}

void WhipTasks(void) {
  bool pressed = (int)TLN_GetInput(INPUT_B) != 0;

  /* Rising-edge trigger: start a new swing only on a fresh press and while
   * no swing is already running. */
  if ((int)pressed && !prev_pressed && !WhipIsActive()) {
    swing_frame = 0;
    swing_facing_right = SimonFacingRight();
    swing_up = (int)TLN_GetInput(INPUT_UP) != 0;
  }
  prev_pressed = pressed;

  render_pending = false;

  if (!WhipIsActive()) {
    return;
  }

  if (swing_frame < total_visible_frames()) {
    last_rendered_stage = frame_to_stage(swing_frame);
    render_pending = true; /* WhipRender() will place sprites after SimonTasks() */
  } else {
    /* Retract phase: hide all segments. */
    disable_all_segments();
  }

  swing_frame++;

  /* Animation complete — ensure all segments are disabled. */
  if (swing_frame >= WHIP_DURATION) {
    render_pending = false;
    disable_all_segments();
  }
}

bool WhipIsUp(void) { return (int)(WhipIsActive() && (int)swing_up) != 0; }

void WhipRender(void) {
  if (!render_pending) {
    return;
  }
  int stage = last_rendered_stage;
  int sprite_x = SimonGetScreenX();
  int sprite_y = SimonGetScreenY();
  const WhipStage *active_stages = (int)swing_up ? up_stages : stages;
  int count = active_stages[stage].count;

  for (int seg = 0; seg < MAX_WHIP_SPRITES; seg++) {
    if (seg < count) {
      const WhipSeg *segment = &active_stages[stage].segs[seg];
      int world_x;
      bool flip_h;
      if (swing_facing_right) {
        world_x = sprite_x + segment->dx;
        flip_h = segment->flip_h;
      } else {
        world_x = sprite_x + (SIMON_SPRITE_W - segment->dx - SEG_SIZE);
        flip_h = ((!segment->flip_h) != 0);
      }
      TLN_SetSpriteSet(WHIP_SPRITE_BASE + seg, spriteset);
      TLN_SetSpritePicture(WHIP_SPRITE_BASE + seg, segment->pic);
      TLN_EnableSpriteFlag(WHIP_SPRITE_BASE + seg, FLAG_FLIPX, flip_h);
      TLN_EnableSpriteFlag(WHIP_SPRITE_BASE + seg, FLAG_FLIPY, segment->flip_v);
      int y_offset = (int)SimonIsCrouching() ? 12 : 0;
      TLN_SetSpritePosition(WHIP_SPRITE_BASE + seg, world_x, sprite_y + segment->dy + y_offset);
    } else {
      TLN_DisableSprite(WHIP_SPRITE_BASE + seg);
    }
  }
}
