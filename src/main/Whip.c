#include "Whip.h"

#include <stdbool.h>

#include "GridSpriteset.h"
#include "LoadFile.h" /* FileOpen() – honours TLN_SetLoadPath()  */
#include "Simon.h"    /* SimonGetScreenX/Y, SimonFacingRight      */
#include "Tilengine.h"

#define SEG_SIZE       8  /* each subsprite is 8x8 px                     */
#define SIMON_SPRITE_W 32 /* width of Simon's sprite in pixels             */
#define CROUCH_Y_OFF   12 /* vertical offset when Simon is crouching       */
#define NUM_STAGES     3  /* number of whip animation stages               */
#define WHIP_LINGER    1  /* extra frames whip sprites stay after Simon reverts */

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

static MapSection stages;      /* horizontal swing (# main) */
static MapSection up_stages;   /* upward swing    (# up)   */
static MapSection down_stages; /* downward swing  (# down) */

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

/* True when the current swing was triggered with INPUT_DOWN held while airborne. */
static bool swing_down;

/* Edge-detect state: prevents re-triggering while INPUT_B is held. */
static bool prev_pressed;

/* True on frames where visible whip sprites should be placed by WhipRender(). */
static bool render_pending;

/* Linger countdown: whip sprites stay visible this many extra frames after
 * the swing ends so the final whip frame outlasts Simon's whip pose. */
static int linger_count;

static void disable_all_segments(void) {
  for (int i = 0; i < MAX_WHIP_SPRITES; i++) {
    TLN_DisableSprite(WHIP_SPRITE_BASE + i);
  }
}

void whip_init(void) {
  spriteset = load_grid_spriteset("whip0", &spriteset_bmp);
  swing_frame = WHIP_DURATION; /* inactive */
  swing_up = false;
  swing_down = false;
  prev_pressed = false;
  linger_count = 0;
  disable_all_segments();

  /* Load both swing directions from a single file. */
  load_map_section("whip0_map.txt", "main", NUM_STAGES, MAX_WHIP_SPRITES, &stages);
  load_map_section("whip0_map.txt", "up", NUM_STAGES, MAX_WHIP_SPRITES, &up_stages);
  load_map_section("whip0_map.txt", "down", NUM_STAGES, MAX_WHIP_SPRITES, &down_stages);
}

void whip_deinit(void) {
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
    stages.stages[i].count = 0;
  }
}

bool whip_is_active(void) { return (bool)(swing_frame < WHIP_DURATION); }

int whip_get_stage(void) {
  if (!whip_is_active()) {
    return 0;
  }
  return last_rendered_stage;
}

void whip_tasks(void) {
  bool pressed = TLN_GetInput(INPUT_B);

  /* Rising-edge trigger: start a new swing only on a fresh press and while
   * no swing is already running (including the linger tail). */
  if ((int)pressed && !prev_pressed && !whip_is_active() && linger_count == 0) {
    swing_frame = 0;
    swing_facing_right = simon_facing_right();
    swing_up = TLN_GetInput(INPUT_UP);
    swing_down = (bool)(!swing_up && (int)TLN_GetInput(INPUT_DOWN) && (int)simon_is_jumping());
  }
  prev_pressed = pressed;

  render_pending = false;

  /* Linger phase: whip sprites stay visible while Simon has already reverted
   * to his standing animation. */
  if (linger_count > 0) {
    linger_count--;
    if (linger_count > 0) {
      render_pending = true;
    } else {
      disable_all_segments();
    }
    return;
  }

  if (!whip_is_active()) {
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

  /* Animation complete — begin linger so the final whip frame stays visible
   * one frame past the point where Simon returns to his standing pose. */
  if (swing_frame >= WHIP_DURATION) {
    linger_count = WHIP_LINGER;
  }
}

bool whip_is_up(void) { return (bool)((int)whip_is_active() && (int)swing_up); }

bool whip_is_down(void) { return (bool)((int)whip_is_active() && (int)swing_down); }

void whip_render(void) {
  if (!render_pending) {
    return;
  }
  int stage = last_rendered_stage;
  int sprite_x = simon_get_screen_x();
  int sprite_y = simon_get_screen_y();
  const MapSection* active_section;
  if (swing_up) {
    active_section = &up_stages;
  } else if (swing_down) {
    active_section = &down_stages;
  } else {
    active_section = &stages;
  }
  int count = active_section->stages[stage].count;

  for (int seg = 0; seg < MAX_WHIP_SPRITES; seg++) {
    if (seg < count) {
      const MapSeg* segment = &active_section->stages[stage].segs[seg];
      int world_x;
      bool flip_h;
      if (swing_facing_right) {
        world_x = sprite_x + segment->dx;
        flip_h = segment->flip_h;
      } else {
        world_x = sprite_x + (SIMON_SPRITE_W - segment->dx - SEG_SIZE);
        flip_h = (bool)(!segment->flip_h);
      }
      TLN_SetSpriteSet(WHIP_SPRITE_BASE + seg, spriteset);
      TLN_SetSpritePicture(WHIP_SPRITE_BASE + seg, segment->pic);
      TLN_EnableSpriteFlag(WHIP_SPRITE_BASE + seg, FLAG_FLIPX, flip_h);
      TLN_EnableSpriteFlag(WHIP_SPRITE_BASE + seg, FLAG_FLIPY, segment->flip_v);
      int y_offset = (int)simon_is_crouching() ? CROUCH_Y_OFF : 0;
      TLN_SetSpritePosition(WHIP_SPRITE_BASE + seg, world_x, sprite_y + segment->dy + y_offset);
    } else {
      TLN_DisableSprite(WHIP_SPRITE_BASE + seg);
    }
  }
}
