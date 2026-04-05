#include "Simon.h"
#include "SimonCollision.h"

#include <stdlib.h>
#include <string.h>

#include "LoadFile.h"
#include "Sandblock.h"
#include "Tilengine.h"
#include "Whip.h"

#define HANGTIME 8
#define TERM_VELOCITY 10
#define AIR_TURN_DELAY 6
#define SIMON_HEIGHT 48
#define SIMON_WIDTH 32          /* total sprite width  (2 × 16 px tiles) */
#define SIMON_SEG_W 16          /* width of one subsprite tile           */
#define SIMON_MAX_STAGES 8      /* max animation stages per section      */
#define SIMON_MAX_SEGS 8        /* max subsprites per stage              */
#define WALK_FRAMES_PER_STAGE 8 /* game frames each walk frame is shown  */
#define JUMP_ARC_LEN_SHORT 45
#define JUMP_ARC_LEN_TALL 46
#define JUMP_ARC_LEN_HIGHER 48
#define BRIDGE_FLOOR_Y 32767
#define CEILING_FALL_TV 8
#define CEILING_GAP 0 /* empty pixel rows between Simon's head and ceiling tile */

/* Gradual acceleration after falling from a ceiling hang.
 * Odd velocity steps hold for 3 frames, even steps for 2, until CEILING_FALL_TV.
 * {1, 1, 1, 2, 2, 3, 3, 3, 4, 4, 5, 5, 5, 6, 6, 7, 7, 7, 8} */
static int ceiling_fall_dy_at(int frame) {
  int velocity = 1;
  int i = 0;
  while (velocity < CEILING_FALL_TV) {
    int count = (velocity & 1) ? 3 : 2;
    if (frame < i + count) {
      return velocity;
    }
    i += count;
    velocity++;
  }
  return CEILING_FALL_TV;
}

/* Short arc (tap) */
static const int jump_arc_dy_short[JUMP_ARC_LEN_SHORT] = {
    -1, -5, -4, -5, -3, -4, -3, -2, -3, -1, -2, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,  0,  1,  1,  2,  1,  3,  2,  3,  4,  3,  5,  4,  5, 5, 6, 6, 7, 7, 8, 7, 8};

/* Tall arc (hold 2 frames) */
static const int jump_arc_dy_tall[JUMP_ARC_LEN_TALL] = {
    -1, -5, -4, -5, -4, -4, -3, -3, -3, -2, -2, -1, -2, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,  0,  0,  1,  2,  1,  2,  2,  3,  3,  4,  4,  5,  4,  5, 5, 6, 6, 7, 7, 8, 7, 8};

/* Higher arc (hold 3+ frames) */
static const int jump_arc_dy_higher[JUMP_ARC_LEN_HIGHER] = {
    -1, -5, -4, -5, -4, -5, -3, -4, -3, -2, -3, -1, -2, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,  0,  0,  1,  1,  2,  1,  3,  2,  3,  4,  3,  5,  4,  5,  5, 6, 6, 6, 7, 7, 8, 7, 8};

#define PROBE_X_START 4
#define PROBE_X_STEP 16
#define PROBE_X_LIMIT 44
#define PROBE_X_OFFSET 23

#define PROBE_Y_OFFSET 36

typedef enum {
  DIR_NONE,
  DIR_LEFT,
  DIR_RIGHT,
} Direction;

typedef struct {
  int pic, dx, dy;
} SimonSeg;
typedef struct {
  SimonSeg segs[SIMON_MAX_SEGS];
  int count;
} SimonStage;
typedef struct {
  SimonStage stages[SIMON_MAX_STAGES];
  int num_stages;
} SimonSection;

static TLN_Spriteset simon;
static TLN_Bitmap simon_bmp;
static SimonSection sec_stand, sec_walk, sec_jump, sec_teeter, sec_crouch, sec_crouch_walk,
    sec_whip, sec_whip_jump, sec_crouch_whip, sec_whip_up, sec_whip_jump_up;
static int walk_anim_frame;

static Coords2d position;
static int y_velocity = 0;
static int apex_hang = 0;
static int jump_frame = 0;
static int ceiling_hang = 0;
static int ceiling_fall_frame = -1;
static bool jump_arc_tall = false;
static bool jump_arc_higher = false;
static bool jump_arc_committed = false;
static bool jump_was_released = true;
static SimonState state;
static Direction direction;

static bool camera_frozen = false;

static int layer_width; /* cached TLN_GetLayerWidth(1) — set once in SimonInit */

/* Per-frame snapshot of active, non-falling sandblocks — built once at the
 * top of SimonTasks() and consumed by all three collision functions. */
static SandblockState sb_cache[MAX_SANDBLOCKS];
static int sb_count;

static Direction air_dir = DIR_NONE;
static int dir_change_timer = 0;
static Direction prev_input = DIR_NONE;
static int move_frame = 0;

/* When set, replaces tile-based floor collision for the current frame.
 * BRIDGE_FLOOR_Y = inactive (no override). Set before calling SimonTasks(). */
static int bridge_floor = BRIDGE_FLOOR_Y;
/* Tolerance window in pixels, scaled 0-8 with bridge progress.
 * At 0 Simon sits exactly on top; at 8 he can stand 8 px into the bridge. */
static int bridge_tolerance = 0;

void SimonSetBridgeFloor(int feet_y) { bridge_floor = feet_y; }
void SimonSetBridgeTolerance(int tol) { bridge_tolerance = tol; }
void SimonClearBridgeFloor(void) {
  bridge_floor = BRIDGE_FLOOR_Y;
  bridge_tolerance = 0;
}

/* ---------------------------------------------------------------------------
 * Internal helpers: spriteset loader, section parser, renderer
 * ------------------------------------------------------------------------- */

static TLN_Spriteset load_grid_spriteset(const char *txt_name, const char *png_name,
                                         TLN_Bitmap *out_bitmap) {
  FILE *file = FileOpen(txt_name);
  if (file == NULL) {
    return NULL;
  }

  int tw = 0;
  int th = 0;
  int cols = 0;

  char line[64];
  while (fgets(line, sizeof(line), file) != NULL) {
    int v;
    if (sscanf(line, " w = %d", &v) == 1) {
      tw = v;
    } else if (sscanf(line, " h = %d", &v) == 1) {
      th = v;
    } else if (sscanf(line, " cols = %d", &v) == 1) {
      cols = v;
    }
  }
  fclose(file);

  if (tw <= 0 || th <= 0 || cols <= 0) {
    return NULL;
  }

  TLN_Bitmap bmp = TLN_LoadBitmap(png_name);
  if (bmp == NULL) {
    return NULL;
  }

  int rows = TLN_GetBitmapHeight(bmp) / th;
  int total = rows * cols;

  TLN_SpriteData *data = (TLN_SpriteData *)malloc((size_t)total * sizeof(TLN_SpriteData));
  if (data == NULL) {
    TLN_DeleteBitmap(bmp);
    return NULL;
  }

  for (int row = 0; row < rows; row++) {
    for (int column = 0; column < cols; column++) {
      TLN_SpriteData *spritedata = &data[(row * cols) + column];
      snprintf(spritedata->name, sizeof(spritedata->name), "s%d", (row * cols) + column);
      spritedata->x = column * tw;
      spritedata->y = row * th;
      spritedata->w = tw;
      spritedata->h = th;
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
 * Parses a single named group from a simon_map.txt-style file into *out.
 * Group format mirrors whip0_map0.txt: "# name" header, then "N:" stage
 * headers, then "sP = ( dx, dy)" subsprite lines.  No flip flags are used
 * for Simon — mirroring is applied uniformly when facing left.
 */
static void load_simon_section(const char *filename, const char *section, SimonSection *out) {
  out->num_stages = 0;
  for (int stage = 0; stage < SIMON_MAX_STAGES; stage++) {
    out->stages[stage].count = 0;
  }

  FILE *file = FileOpen(filename);
  if (file == NULL) {
    return;
  }

  bool in_section = false;
  int cur_stage = -1;
  char line[128];
  while (fgets(line, sizeof(line), file) != NULL) {
    if (line[0] == '#') {
      char name[64] = "";
      sscanf(line, "# %63s", name);
      in_section = (strcmp(name, section) == 0);
      cur_stage = -1;
      continue;
    }
    if (!in_section) {
      continue;
    }

    int idx = -1;
    if (sscanf(line, " %d :", &idx) == 1 && idx >= 0 && idx < SIMON_MAX_STAGES) {
      cur_stage = idx;
      if (cur_stage + 1 > out->num_stages) {
        out->num_stages = cur_stage + 1;
      }
      continue;
    }

    if (cur_stage < 0 || out->stages[cur_stage].count >= SIMON_MAX_SEGS) {
      continue;
    }

    int pic = -1;
    int dx = 0;
    int dy = 0;
    if (sscanf(line, " s%d = ( %d , %d )", &pic, &dx, &dy) < 3 || pic < 0) {
      continue;
    }

    SimonSeg *seg = &out->stages[cur_stage].segs[out->stages[cur_stage].count++];
    seg->pic = pic;
    seg->dx = dx;
    seg->dy = dy;
  }
  fclose(file);
}

/* Renders one stage of a section using the current position and direction. */
static void render_section_stage(const SimonSection *sec, int stage_idx) {
  if (sec->num_stages == 0) {
    for (int i = 0; i < MAX_SIMON_SPRITES; i++) {
      TLN_DisableSprite(SIMON_SPRITE_BASE + i);
    }
    return;
  }
  if (stage_idx >= sec->num_stages) {
    stage_idx = sec->num_stages - 1;
  }
  const SimonStage *stage = &sec->stages[stage_idx];
  bool facing_right = (direction == DIR_RIGHT);
  for (int i = 0; i < MAX_SIMON_SPRITES; i++) {
    if (i < stage->count) {
      const SimonSeg *seg = &stage->segs[i];
      int worldx =
          position.x + ((int)facing_right ? seg->dx : (SIMON_WIDTH - seg->dx - SIMON_SEG_W));
      TLN_SetSpriteSet(SIMON_SPRITE_BASE + i, simon);
      TLN_SetSpritePicture(SIMON_SPRITE_BASE + i, seg->pic);
      TLN_EnableSpriteFlag(SIMON_SPRITE_BASE + i, FLAG_FLIPX, (bool)!facing_right);
      TLN_EnableSpriteFlag(SIMON_SPRITE_BASE + i, FLAG_FLIPY, false);
      TLN_SetSpritePosition(SIMON_SPRITE_BASE + i, worldx, position.y + seg->dy);
    } else {
      TLN_DisableSprite(SIMON_SPRITE_BASE + i);
    }
  }
}

/*
 * Selects the correct section and stage for the current animation state and
 * calls render_section_stage().  Called at the end of every SimonTasks()
 * and from any function that moves Simon outside the normal update loop.
 */
static void render_current_state(void) {
  const SimonSection *sec;
  int stage = 0;
  if (WhipIsActive()) {
    if (state == SIMON_JUMPING) {
      sec = (int)WhipIsUp() ? &sec_whip_jump_up : &sec_whip_jump;
    } else if (state == SIMON_CROUCHING || state == SIMON_CROUCH_WALKING ||
               state == SIMON_CROUCH_WHIPPING) {
      sec = &sec_crouch_whip;
    } else if (WhipIsUp()) {
      sec = &sec_whip_up;
    } else {
      sec = &sec_whip;
    }
    stage = WhipGetStage();
    if (stage >= sec->num_stages) {
      stage = sec->num_stages - 1;
    }
  } else {
    switch (state) {
    case SIMON_WALKING:
      sec = &sec_walk;
      if (sec->num_stages > 0) {
        stage = (walk_anim_frame / WALK_FRAMES_PER_STAGE) % sec->num_stages;
      }
      break;
    case SIMON_JUMPING:
      sec = &sec_jump;
      break;
    case SIMON_TEETER:
      sec = &sec_teeter;
      break;
    case SIMON_CROUCHING:
    case SIMON_CROUCH_WHIPPING:
      sec = &sec_crouch;
      break;
    case SIMON_CROUCH_WALKING:
      sec = &sec_crouch_walk;
      if (sec_crouch_walk.num_stages > 0) {
        stage = (walk_anim_frame / WALK_FRAMES_PER_STAGE) % sec_crouch_walk.num_stages;
      }
      break;
    default:
      sec = &sec_stand;
      break;
    }
  }
  render_section_stage(sec, stage);
}

/* ---------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void SimonInit(void) {
  load_col_definition();
  simon = load_grid_spriteset("simon.txt", "simon.png", &simon_bmp);
  load_simon_section("simon_map.txt", "stand", &sec_stand);
  load_simon_section("simon_map.txt", "walk", &sec_walk);
  load_simon_section("simon_map.txt", "jump", &sec_jump);
  load_simon_section("simon_map.txt", "teeter", &sec_teeter);
  load_simon_section("simon_map.txt", "crouch", &sec_crouch);
  load_simon_section("simon_map.txt", "crouch-walk", &sec_crouch_walk);
  load_simon_section("simon_map.txt", "whip", &sec_whip);
  load_simon_section("simon_map.txt", "jump-whip", &sec_whip_jump);
  load_simon_section("simon_map.txt", "crouch-whip", &sec_crouch_whip);
  load_simon_section("simon_map.txt", "whip-up", &sec_whip_up);
  load_simon_section("simon_map.txt", "jump-whip-up", &sec_whip_jump_up);

  layer_width = TLN_GetLayerWidth(1);
  state = SIMON_IDLE;
  direction = DIR_RIGHT;
  walk_anim_frame = 0;

  render_current_state();
}

void SimonDeinit(void) {
  if (simon != NULL) {
    TLN_DeleteSpriteset(simon);
    simon = NULL;
  }
  if (simon_bmp != NULL) {
    TLN_DeleteBitmap(simon_bmp);
    simon_bmp = NULL;
  }
}

void SimonBringToFront(void) {
  /* Disabling then re-rendering all segments moves them to the tail of the
   * engine's render list, ensuring Simon draws on top of all other sprites. */
  for (int i = 0; i < MAX_SIMON_SPRITES; i++) {
    TLN_DisableSprite(SIMON_SPRITE_BASE + i);
  }
  render_current_state();
}

void SimonFreezeCamera(void) { camera_frozen = true; }

void SimonSetState(SimonState new_state) {
  if (state == new_state) {
    return;
  }
  state = new_state;
  if (state == SIMON_JUMPING) {
    jump_frame = 0;
    jump_arc_tall = false;
    jump_arc_higher = false;
    jump_arc_committed = false;
    jump_was_released = false;
    ceiling_hang = 0;
    ceiling_fall_frame = -1;
    y_velocity = 0;
  } else if (state == SIMON_WALKING) {
    walk_anim_frame = 0;
  }
}

static void update_facing(Direction input) {
  if ((input == DIR_RIGHT && direction == DIR_LEFT) ||
      (input == DIR_LEFT && direction == DIR_RIGHT)) {
    direction = input;
  }
}

/**
 * Updates air-throttle state and returns whether Simon is changing direction
 * mid-air.
 */
static bool update_air_throttle(Direction input) {
  if (state != SIMON_JUMPING) {
    air_dir = input;
    dir_change_timer = 0;
  } else if (input == DIR_NONE) {
    /* released in the air — treat next press as a new direction change */
    air_dir = DIR_NONE;
  }
  bool changing_dir = (state == SIMON_JUMPING && input != DIR_NONE && input != air_dir) != 0;
  if (changing_dir) {
    dir_change_timer++;
  } else {
    dir_change_timer = 0;
  }
  return changing_dir;
}

/** Commits the direction change and returns the desired dx before collision. */
static int execute_move(Direction input, bool changing_dir) {
  if (changing_dir) {
    air_dir = input; /* commit new direction after delay */
  }
  update_facing(input); /* flip sprite only when movement commits */

  int dx = 0;
  if (input == DIR_RIGHT) {
    dx = (++move_frame % 4 == 0) ? 2 : 1;
  } else if (input == DIR_LEFT) {
    dx = (++move_frame % 4 == 0) ? -2 : -1;
  }
  return dx;
}

/**
 * Handles air-throttle tracking and drives the movement state machine.
 * Returns the desired horizontal displacement before collision resolution.
 */
static int apply_movement(Direction input) {
  bool changing_dir = update_air_throttle(input);

  bool first_frame = (prev_input == DIR_NONE && input != DIR_NONE) != 0;
  prev_input = input;

  int dx = 0;
  switch (state) {
  case SIMON_TEETER:
  case SIMON_IDLE:
    if (input) {
      SimonSetState(SIMON_WALKING);
    }
    break;
  case SIMON_WALKING:
  case SIMON_JUMPING:
    if (!first_frame && (!changing_dir || dir_change_timer > AIR_TURN_DELAY)) {
      dx = execute_move(input, changing_dir);
    } else {
      move_frame = 0;
    }
    if (state == SIMON_WALKING && !input) {
      SimonSetState(SIMON_IDLE);
    }
    break;
  case SIMON_CROUCHING:
    /* Directional input while crouching starts a crouch-walk. */
    if (input) {
      SimonSetState(SIMON_CROUCH_WALKING);
      dx = execute_move(input, changing_dir);
    }
    break;
  case SIMON_CROUCH_WALKING:
    if (!first_frame && (!changing_dir || dir_change_timer > AIR_TURN_DELAY)) {
      dx = execute_move(input, changing_dir);
    } else {
      move_frame = 0;
    }
    if (!input) {
      SimonSetState(SIMON_CROUCHING);
    }
    break;
  case SIMON_CROUCH_WHIPPING:
    /* No movement during a crouched whip swing. */
    break;
  }
  return dx;
}

/**
 * Applies ceiling/floor collision and detects landing.
 * \param start_y_velocity  Vertical velocity captured before advance_gravity() was called.
 */
static void apply_collisions(int start_y_velocity, int dx, int width) {
  int arc_len = (int)jump_arc_higher ? JUMP_ARC_LEN_HIGHER
                : (int)jump_arc_tall ? JUMP_ARC_LEN_TALL
                                     : JUMP_ARC_LEN_SHORT;
  int dy;
  if (state == SIMON_JUMPING) {
    if (ceiling_hang > 0) {
      ceiling_hang--;
      dy = 0; /* hang motionless against ceiling */
      y_velocity = 0;
    } else if (ceiling_fall_frame >= 0) {
      /* gradual acceleration after ceiling hang */
      dy = ceiling_fall_dy_at(ceiling_fall_frame++);
      y_velocity = dy;
    } else {
      const int *arc = (int)jump_arc_higher ? jump_arc_dy_higher
                       : (int)jump_arc_tall ? jump_arc_dy_tall
                                            : jump_arc_dy_short;
      if (jump_frame < arc_len) {
        dy = arc[jump_frame++];
      } else {
        dy = CEILING_FALL_TV; /* constant fall after arc ends */
      }
      y_velocity = dy; /* proxy: keeps landing detection working */
    }
  } else {
    /* non-jumping states: original velocity-based movement */
    dy = (y_velocity > 0 ? y_velocity / 3 : y_velocity >> 2);
  }

  if (dx == 0 && dy == 0) {
    return;
  }

  resolve_collision(position.scroll_x, position.x, position.y, &dx, &dy);
  if (dx > 0) {
    if (!camera_frozen && position.scroll_x < layer_width - width && position.x >= 112) {
      position.scroll_x += dx;
    } else if (position.x < 112 || position.x < width - 16) {
      position.x += dx;
    }
  } else if (dx < 0) {
    if (!camera_frozen && position.scroll_x > 0 && position.x <= 128) {
      position.scroll_x += dx;
    } else if (position.x > -4) {
      position.x += dx;
    }
  }
  position.y += dy;
  if (start_y_velocity > 0 && y_velocity == 0) {
    SimonSetState(SIMON_IDLE);
  }
  if (position.y > TLN_GetHeight()) {
    position.y = 0;
    y_velocity = 0;
    SimonSetState(SIMON_IDLE);
  }
}

void SimonTasks(void) {
  sb_count = SandblockSnapshot(sb_cache);

  Direction input = DIR_NONE;
  bool jump = false;
  bool crouch_held = (int)TLN_GetInput(INPUT_DOWN) != 0;

  /* While whipping in the air, allow directional movement but suppress jump.
   * On the ground, suppress all input so Simon stays planted.
   * While crouching (stationary), allow left/right for crouch-walk but not jump. */
  bool whip_airborne = ((int)WhipIsActive() && (state == SIMON_JUMPING)) != 0;
  bool is_crouching = (state == SIMON_CROUCHING || state == SIMON_CROUCH_WALKING ||
                       state == SIMON_CROUCH_WHIPPING) != 0;
  if (!WhipIsActive() || (int)whip_airborne) {
    if (TLN_GetInput(INPUT_LEFT)) {
      input = DIR_LEFT;
    } else if (TLN_GetInput(INPUT_RIGHT)) {
      input = DIR_RIGHT;
    }
    if (!WhipIsActive() && !is_crouching && (int)TLN_GetInput(INPUT_A)) {
      jump = true;
    }
  }

  /* Track whether jump button was released since last jump. */
  if (!TLN_GetInput(INPUT_A)) {
    jump_was_released = true;
  }

  int width = TLN_GetWidth();
  int desired_dx = apply_movement(input);

  if ((int)jump && (int)jump_was_released && state != SIMON_JUMPING) {
    SimonSetState(SIMON_JUMPING);
  }

  /* Arc-type commitment: deferred until the earliest frame the button *could*
   * have been released to distinguish all three hold durations.
   * Frame 1: if INPUT_A already released → short arc (tap).
   * Frame 2: if INPUT_A still held → higher arc (3+ frames); released → tall. */
  if (state == SIMON_JUMPING && !jump_arc_committed) {
    if (jump_frame == 1 && !TLN_GetInput(INPUT_A)) {
      jump_arc_committed = true; /* short: tall and higher stay false */
    } else if (jump_frame == 2) {
      jump_arc_committed = true;
      if (TLN_GetInput(INPUT_A)) {
        jump_arc_higher = true;
      } else {
        jump_arc_tall = true;
      }
    }
  }

  int start_y_velocity = y_velocity;
  apply_collisions(start_y_velocity, desired_dx, width);

  /* If collisions landed Simon into IDLE but a direction is still held,
   * promote immediately to WALKING so the idle sprite never shows for one
   * frame. */
  if (state == SIMON_IDLE && input != DIR_NONE) {
    SimonSetState(SIMON_WALKING);
  }

  /* Crouch: down held while on the ground (not jumping, not whipping). */
  bool on_ground = (state != SIMON_JUMPING);
  if ((int)crouch_held && (int)on_ground && !WhipIsActive()) {
    if (state == SIMON_WALKING) {
      SimonSetState(SIMON_CROUCH_WALKING);
    } else if (state != SIMON_CROUCHING && state != SIMON_CROUCH_WALKING &&
               state != SIMON_CROUCH_WHIPPING) {
      SimonSetState(SIMON_CROUCHING);
    }
  } else if (state == SIMON_CROUCH_WHIPPING) {
    /* S released while crouch-whipping: hold crouch until whip finishes. */
    if (!WhipIsActive()) {
      SimonSetState((int)crouch_held ? SIMON_CROUCHING : SIMON_IDLE);
    }
  } else if ((state == SIMON_CROUCHING || state == SIMON_CROUCH_WALKING) &&
             (!crouch_held || !on_ground)) {
    SimonSetState(state == SIMON_CROUCH_WALKING ? SIMON_WALKING : SIMON_IDLE);
  }

  /* Transition crouching states to CROUCH_WHIPPING when whip fires. */
  if ((int)WhipIsActive() && (int)on_ground &&
      (state == SIMON_CROUCHING || state == SIMON_CROUCH_WALKING)) {
    SimonSetState(SIMON_CROUCH_WHIPPING);
  }

  /* Teeter: idle on the bridge surface with no player input. */
  if (state == SIMON_IDLE && bridge_floor != BRIDGE_FLOOR_Y && input == DIR_NONE) {
    SimonSetState(SIMON_TEETER);
  } else if (state == SIMON_TEETER && bridge_floor == BRIDGE_FLOOR_Y) {
    SimonSetState(SIMON_IDLE);
  }

  if (state == SIMON_WALKING || state == SIMON_CROUCH_WALKING) {
    walk_anim_frame++;
  }
  render_current_state();
}

int SimonGetPosition(void) { return position.scroll_x; }

void SimonSetPosition(Coords2d pos) {
  position.x = pos.x;
  position.y = pos.y;
  position.scroll_x = 0;
  render_current_state();
}

void SimonPushRight(int pixels) {
  position.x += pixels;
  /* clamp to one sprite-width past the right screen edge */
  if (position.x > TLN_GetWidth()) {
    position.x = TLN_GetWidth();
  }
  render_current_state();
}

int SimonGetScreenX(void) { return position.x; }

void SimonSetScreenX(int screen_x) {
  position.x = screen_x;
  render_current_state();
}

void SimonSetFeetY(int feet_y) {
  SimonPinFeetY(feet_y);

  /* Pinning feet to a surface counts as landing: cancel any in-progress jump
   * so the jump sprite clears and the player can jump again next frame. */
  if (state == SIMON_JUMPING) {
    SimonSetState(SIMON_IDLE);
  }
}

void SimonPinFeetY(int feet_y) {
  /* Position-only correction: does not change state.
   * Zeroes y_velocity and resets apex_hang so advance_gravity cannot accumulate
   * velocity on surfaces without collision tiles (e.g. the drawbridge deck). */
  position.y = feet_y - SIMON_HEIGHT;
  y_velocity = 0;
  apex_hang = 0;
  render_current_state();
}

int SimonGetFeetY(void) { return position.y + SIMON_HEIGHT; }

void SimonSetWorldX(int new_world_x) { position.scroll_x = new_world_x; }

int SimonGetScreenY(void) { return position.y; }

bool SimonIsCrouching(void) {
  return (state == SIMON_CROUCHING || state == SIMON_CROUCH_WALKING ||
          state == SIMON_CROUCH_WHIPPING) != 0;
}

bool SimonFacingRight(void) { return direction == DIR_RIGHT; }
