#include "Simon.h"
#include "SimonCollision.h"

#include "GridSpriteset.h"
#include "LoadFile.h"
#include "Sandblock.h"
#include "Tilengine.h"
#include "Whip.h"

#define HANGTIME               8
#define TERM_VELOCITY          10
#define AIR_TURN_DELAY         6
#define SIMON_HEIGHT           48
#define SIMON_WIDTH            32 /* total sprite width  (2 × 16 px tiles) */
#define SIMON_SEG_W            16 /* width of one subsprite tile           */
#define SIMON_MAX_STAGES       8  /* max animation stages per section      */
#define SIMON_MAX_SEGS         8  /* max subsprites per stage              */
#define WALK_FRAMES_PER_STAGE  8  /* game frames each walk frame is shown  */
#define JUMP_ARC_LEN_SHORT     45
#define JUMP_ARC_LEN_TALL      46
#define JUMP_ARC_LEN_HIGHER    48
#define BRIDGE_FLOOR_Y         32767
#define CEILING_FALL_TV        8
#define CEILING_GAP            0   /* empty pixel rows between Simon's head and ceiling tile */
#define SCROLL_RIGHT_THRESHOLD 112 /* screen-x at which camera begins scrolling right */
#define SCROLL_LEFT_THRESHOLD  128 /* screen-x at which camera begins scrolling left  */

/* Gradual acceleration after falling from a ceiling hang.
 * Odd velocity steps hold for 3 frames, even steps for 2, until CEILING_FALL_TV.
 * {1, 1, 1, 2, 2, 3, 3, 3, 4, 4, 5, 5, 5, 6, 6, 7, 7, 7, 8} */
static int ceiling_fall_dy_at(int frame) {
  int vel = 1;
  int acc = 0;
  while (vel < CEILING_FALL_TV) {
    int count = (vel & 1) ? 3 : 2;
    if (frame < acc + count) {
      return vel;
    }
    acc += count;
    vel++;
  }
  return CEILING_FALL_TV;
}

/* Gradual acceleration when walking off a ledge.
 * Prepends {-1, 0} to the ceiling fall sequence.
 * {-1, 0, 1, 1, 1, 2, 2, 3, 3, 3, 4, 4, 5, 5, 5, 6, 6, 7, 7, 7, 8} */
static int edge_fall_dy_at(int frame) {
  if (frame == 0) {
    return -1;
  }
  if (frame == 1) {
    return 0;
  }
  return ceiling_fall_dy_at(frame - 2);
}

/* Short arc (tap) */
static const int jump_arc_dy_short[JUMP_ARC_LEN_SHORT] = {-1, -5, -4, -5, -3, -4, -3, -2, -3, -1, -2, -1, -1, 0, 0,
                                                          0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  2,  1, 3,
                                                          2,  3,  4,  3,  5,  4,  5,  5,  6,  6,  7,  7,  8,  7, 8};

/* Tall arc (hold 2 frames) */
static const int jump_arc_dy_tall[JUMP_ARC_LEN_TALL] = {-1, -5, -4, -5, -4, -4, -3, -3, -3, -2, -2, -1, -2, -1, 0, 0,
                                                        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  2,  1,  2,  2, 3,
                                                        3,  4,  4,  5,  4,  5,  5,  6,  6,  7,  7,  8,  7,  8};

/* Higher arc (hold 3+ frames) */
static const int jump_arc_dy_higher[JUMP_ARC_LEN_HIGHER] = {
  -1, -5, -4, -5, -4, -5, -3, -4, -3, -2, -3, -1, -2, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0,  0,  0,  1,  1,  2,  1,  3,  2,  3,  4,  3,  5,  4,  5,  5, 6, 6, 6, 7, 7, 8, 7, 8};

#define PROBE_X_START  4
#define PROBE_X_STEP   16
#define PROBE_X_LIMIT  44
#define PROBE_X_OFFSET 23

#define PROBE_Y_OFFSET 36

typedef enum {
  DIR_NONE,
  DIR_LEFT,
  DIR_RIGHT,
} Direction;

static TLN_Spriteset simon;
static TLN_Bitmap simon_bmp;
static MapSection sec_stand, sec_walk, sec_jump, sec_teeter, sec_crouch, sec_crouch_walk, sec_whip, sec_whip_jump,
  sec_crouch_whip, sec_whip_up, sec_whip_jump_up, sec_whip_jump_down;
static int walk_anim_frame;

static Coords2d position;
static int y_velocity = 0;
static int apex_hang = 0;
static int jump_frame = 0;
static int ceiling_hang = 0;
static int ceiling_fall_frame = -1;
static int edge_fall_frame = -1;
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

void simon_set_bridge_floor(int feet_y) { bridge_floor = feet_y; }

void simon_set_bridge_tolerance(int tol) { bridge_tolerance = tol; }

void simon_clear_bridge_floor(void) {
  bridge_floor = BRIDGE_FLOOR_Y;
  bridge_tolerance = 0;
}

/* ---------------------------------------------------------------------------
 * Internal helpers: renderer
 * ------------------------------------------------------------------------- */

/* Renders one stage of a section using the current position and direction. */
static void render_section_stage(const MapSection* sec, int stage_idx) {
  if (sec->num_stages == 0) {
    for (int i = 0; i < MAX_SIMON_SPRITES; i++) {
      TLN_DisableSprite(SIMON_SPRITE_BASE + i);
    }
    return;
  }
  if (stage_idx >= sec->num_stages) {
    stage_idx = sec->num_stages - 1;
  }
  const MapStage* stage = &sec->stages[stage_idx];
  bool facing_right = (bool)(direction == DIR_RIGHT);
  for (int i = 0; i < MAX_SIMON_SPRITES; i++) {
    if (i < stage->count) {
      const MapSeg* seg = &stage->segs[i];
      int worldx = position.x + ((int)facing_right ? seg->dx : (SIMON_WIDTH - seg->dx - SIMON_SEG_W));
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
  const MapSection* sec;
  int stage = 0;
  if (whip_is_active()) {
    if (state == SIMON_JUMPING) {
      if (whip_is_up()) {
        sec = &sec_whip_jump_up;
      } else if (whip_is_down()) {
        sec = &sec_whip_jump_down;
      } else {
        sec = &sec_whip_jump;
      }
    } else if (state == SIMON_CROUCHING || state == SIMON_CROUCH_WALKING || state == SIMON_CROUCH_WHIPPING) {
      sec = &sec_crouch_whip;
    } else if (whip_is_up()) {
      sec = &sec_whip_up;
    } else {
      sec = &sec_whip;
    }
    stage = whip_get_stage();
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
      case SIMON_JUMPING: sec = &sec_jump; break;
      case SIMON_TEETER: sec = &sec_teeter; break;
      case SIMON_CROUCHING:
      case SIMON_CROUCH_WHIPPING: sec = &sec_crouch; break;
      case SIMON_CROUCH_WALKING:
        sec = &sec_crouch_walk;
        if (sec_crouch_walk.num_stages > 0) {
          stage = (walk_anim_frame / WALK_FRAMES_PER_STAGE) % sec_crouch_walk.num_stages;
        }
        break;
      default: sec = &sec_stand; break;
    }
  }
  render_section_stage(sec, stage);
}

/* ---------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void simon_init(void) {
  load_col_definition();
  simon = load_grid_spriteset("simon", &simon_bmp);
  load_map_section("simon_map.txt", "stand", SIMON_MAX_STAGES, SIMON_MAX_SEGS, &sec_stand);
  load_map_section("simon_map.txt", "walk", SIMON_MAX_STAGES, SIMON_MAX_SEGS, &sec_walk);
  load_map_section("simon_map.txt", "jump", SIMON_MAX_STAGES, SIMON_MAX_SEGS, &sec_jump);
  load_map_section("simon_map.txt", "teeter", SIMON_MAX_STAGES, SIMON_MAX_SEGS, &sec_teeter);
  load_map_section("simon_map.txt", "crouch", SIMON_MAX_STAGES, SIMON_MAX_SEGS, &sec_crouch);
  load_map_section("simon_map.txt", "crouch-walk", SIMON_MAX_STAGES, SIMON_MAX_SEGS, &sec_crouch_walk);
  load_map_section("simon_map.txt", "whip", SIMON_MAX_STAGES, SIMON_MAX_SEGS, &sec_whip);
  load_map_section("simon_map.txt", "jump-whip", SIMON_MAX_STAGES, SIMON_MAX_SEGS, &sec_whip_jump);
  load_map_section("simon_map.txt", "crouch-whip", SIMON_MAX_STAGES, SIMON_MAX_SEGS, &sec_crouch_whip);
  load_map_section("simon_map.txt", "whip-up", SIMON_MAX_STAGES, SIMON_MAX_SEGS, &sec_whip_up);
  load_map_section("simon_map.txt", "jump-whip-up", SIMON_MAX_STAGES, SIMON_MAX_SEGS, &sec_whip_jump_up);
  load_map_section("simon_map.txt", "jump-whip-down", SIMON_MAX_STAGES, SIMON_MAX_SEGS, &sec_whip_jump_down);

  layer_width = TLN_GetLayerWidth(1);
  state = SIMON_IDLE;
  direction = DIR_RIGHT;
  walk_anim_frame = 0;

  render_current_state();
}

void simon_deinit(void) {
  if (simon != NULL) {
    TLN_DeleteSpriteset(simon);
    simon = NULL;
  }
  if (simon_bmp != NULL) {
    TLN_DeleteBitmap(simon_bmp);
    simon_bmp = NULL;
  }
}

void simon_bring_to_front(void) {
  /* Disabling then re-rendering all segments moves them to the tail of the
   * engine's render list, ensuring Simon draws on top of all other sprites. */
  for (int i = 0; i < MAX_SIMON_SPRITES; i++) {
    TLN_DisableSprite(SIMON_SPRITE_BASE + i);
  }
  render_current_state();
}

void simon_freeze_camera(void) { camera_frozen = true; }

void simon_set_state(SimonState new_state) {
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
    edge_fall_frame = -1;
    y_velocity = 0;
  } else if (state == SIMON_WALKING) {
    walk_anim_frame = 0;
  }
}

static void update_facing(Direction input) {
  if ((input == DIR_RIGHT && direction == DIR_LEFT) || (input == DIR_LEFT && direction == DIR_RIGHT)) {
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
  bool changing_dir = (bool)(state == SIMON_JUMPING && input != DIR_NONE && input != air_dir);
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

  int displacement_x = 0;
  if (input == DIR_RIGHT) {
    displacement_x = (++move_frame % 4 == 0) ? 2 : 1;
  } else if (input == DIR_LEFT) {
    displacement_x = (++move_frame % 4 == 0) ? -2 : -1;
  }
  return displacement_x;
}

/**
 * Handles air-throttle tracking and drives the movement state machine.
 * Returns the desired horizontal displacement before collision resolution.
 */
static int apply_movement(Direction input) {
  bool changing_dir = update_air_throttle(input);

  bool first_frame = (bool)(prev_input == DIR_NONE && input != DIR_NONE);
  prev_input = input;

  int displacement_x = 0;
  switch (state) {
    case SIMON_TEETER:
    case SIMON_IDLE:
      if (input) {
        simon_set_state(SIMON_WALKING);
      }
      break;
    case SIMON_WALKING:
    case SIMON_JUMPING:
      if (!first_frame && (!changing_dir || dir_change_timer > AIR_TURN_DELAY)) {
        displacement_x = execute_move(input, changing_dir);
      } else {
        move_frame = 0;
      }
      if (state == SIMON_WALKING && !input) {
        simon_set_state(SIMON_IDLE);
      }
      break;
    case SIMON_CROUCHING:
      /* Directional input while crouching starts a crouch-walk. */
      if (input) {
        simon_set_state(SIMON_CROUCH_WALKING);
        displacement_x = execute_move(input, changing_dir);
      }
      break;
    case SIMON_CROUCH_WALKING:
      if (!first_frame && (!changing_dir || dir_change_timer > AIR_TURN_DELAY)) {
        displacement_x = execute_move(input, changing_dir);
      } else {
        move_frame = 0;
      }
      if (!input) {
        simon_set_state(SIMON_CROUCHING);
      }
      break;
    case SIMON_CROUCH_WHIPPING:
      /* No movement during a crouched whip swing. */
      break;
  }
  return displacement_x;
}

/* Computes this frame's vertical displacement and updates y_velocity. */
static int compute_displacement_y(void) {
  if (state != SIMON_JUMPING) {
    return (y_velocity > 0 ? y_velocity / 3 : y_velocity >> 2);
  }
  if (ceiling_hang > 0) {
    ceiling_hang--;
    y_velocity = 0;
    return 0;
  }
  if (ceiling_fall_frame >= 0) {
    int displacement_y = ceiling_fall_dy_at(ceiling_fall_frame++);
    y_velocity = displacement_y;
    return displacement_y;
  }
  if (edge_fall_frame >= 0) {
    int displacement_y = edge_fall_dy_at(edge_fall_frame++);
    y_velocity = displacement_y;
    return displacement_y;
  }
  int arc_len;
  const int* arc;
  if (jump_arc_higher) {
    arc_len = JUMP_ARC_LEN_HIGHER;
    arc = jump_arc_dy_higher;
  } else if (jump_arc_tall) {
    arc_len = JUMP_ARC_LEN_TALL;
    arc = jump_arc_dy_tall;
  } else {
    arc_len = JUMP_ARC_LEN_SHORT;
    arc = jump_arc_dy_short;
  }
  int displacement_y = (jump_frame < arc_len) ? arc[jump_frame++] : CEILING_FALL_TV;
  y_velocity = displacement_y;
  return displacement_y;
}

/* Marks sandblocks that Simon is currently standing on. */
static void mark_stood_sandblocks(void) {
  int feet_y = SIMON_COL_BOTTOM_Y(position.y);
  int left_x = SIMON_COL_LEFT_X(position.scroll_x, position.x);
  int right_x = SIMON_COL_RIGHT_X(position.scroll_x, position.x);
  for (int i = 0; i < sb_count; i++) {
    int sb_top = sb_cache[i].world_y;
    int sb_left = sb_cache[i].world_x;
    int sb_right = sb_cache[i].world_x + SANDBLOCK_WIDTH - 1;
    if (right_x >= sb_left && left_x <= sb_right && feet_y == sb_top - 1) {
      sandblock_mark_stood(sb_cache[i].index);
    }
  }
}

/* Clamps downward displacement_y against active sandblock tops. */
static int clamp_sandblock_floor(int displacement_y) {
  int feet_y = SIMON_COL_BOTTOM_Y(position.y);
  int left_x = SIMON_COL_LEFT_X(position.scroll_x, position.x);
  int right_x = SIMON_COL_RIGHT_X(position.scroll_x, position.x);
  for (int i = 0; i < sb_count; i++) {
    int sb_top = sb_cache[i].world_y;
    int sb_left = sb_cache[i].world_x;
    int sb_right = sb_cache[i].world_x + SANDBLOCK_WIDTH - 1;
    if (right_x < sb_left || left_x > sb_right) {
      continue;
    }
    if (feet_y < sb_top && feet_y + displacement_y >= sb_top) {
      displacement_y = sb_top - feet_y - 1;
      sandblock_mark_stood(sb_cache[i].index);
    }
  }
  return displacement_y;
}

/* Applies horizontal displacement with camera scrolling logic. */
static void apply_horizontal_scroll(int displacement_x) {
  int width = TLN_GetWidth();
  if (displacement_x > 0) {
    if (!camera_frozen && position.scroll_x < layer_width - width && position.x >= SCROLL_RIGHT_THRESHOLD) {
      position.scroll_x += displacement_x;
    } else if (position.x < SCROLL_RIGHT_THRESHOLD || position.x < width - SIMON_COL_WIDTH) {
      position.x += displacement_x;
    }
  } else if (displacement_x < 0) {
    if (!camera_frozen && position.scroll_x > 0 && position.x <= SCROLL_LEFT_THRESHOLD) {
      position.scroll_x += displacement_x;
    } else if (position.x > -4) {
      position.x += displacement_x;
    }
  }
}

/* Applies ceiling/floor collision and detects landing. */
static void apply_collisions(int displacement_x) {
  int start_y_velocity = y_velocity;
  int displacement_y = compute_displacement_y();

  mark_stood_sandblocks();

  if (displacement_x == 0 && displacement_y == 0) {
    return;
  }

  resolve_collision(position.scroll_x, position.x, position.y, &displacement_x, &displacement_y);

  if (displacement_y > 0) {
    displacement_y = clamp_sandblock_floor(displacement_y);
  }

  if (y_velocity > 0 && displacement_y < y_velocity) {
    y_velocity = 0;
  }
  apply_horizontal_scroll(displacement_x);
  position.y += displacement_y;
  if (start_y_velocity > 0 && y_velocity == 0) {
    simon_set_state(SIMON_IDLE);
  }
  if (position.y > TLN_GetHeight()) {
    position.y = 0;
    y_velocity = 0;
    simon_set_state(SIMON_IDLE);
  }
}

/**
 * Returns true if there is solid ground directly beneath Simon's collision box.
 * Checks tile collision layer, sandblocks, and the bridge floor override.
 */
static bool has_ground_support(void) {
  int check_y = SIMON_COL_BOTTOM_Y(position.y) + 1;
  int left_x = SIMON_COL_LEFT_X(position.scroll_x, position.x);
  int right_x = SIMON_COL_RIGHT_X(position.scroll_x, position.x);

  /* Tile collision layer */
  TLN_TileInfo tile;
  TLN_GetLayerTile(COLLISION_LAYER, left_x, check_y, &tile);
  if (!tile.empty) {
    return true;
  }
  TLN_GetLayerTile(COLLISION_LAYER, right_x, check_y, &tile);
  if (!tile.empty) {
    return true;
  }

  /* Sandblocks */
  int feet_y = SIMON_COL_BOTTOM_Y(position.y);
  for (int i = 0; i < sb_count; i++) {
    int sb_top = sb_cache[i].world_y;
    int sb_left = sb_cache[i].world_x;
    int sb_right = sb_cache[i].world_x + SANDBLOCK_WIDTH - 1;
    if (right_x >= sb_left && left_x <= sb_right && feet_y == sb_top - 1) {
      return true;
    }
  }

  /* Bridge floor override */
  return (bool)(bridge_floor != BRIDGE_FLOOR_Y);
}

/* ---------------------------------------------------------------------------
 * simon_tasks() helpers — each owns one phase of the per-frame update
 * ------------------------------------------------------------------------- */

typedef struct {
  Direction dir;
  bool jump;
  bool crouch_held;
} SimonInput;

/* Reads gamepad state with whip / crouch suppression rules applied. */
static SimonInput read_input(void) {
  SimonInput inp = {DIR_NONE, false, false};
  inp.crouch_held = TLN_GetInput(INPUT_DOWN);

  bool whip_airborne = (bool)((int)whip_is_active() && (state == SIMON_JUMPING));
  if (!whip_is_active() || (int)whip_airborne) {
    if (TLN_GetInput(INPUT_LEFT)) {
      inp.dir = DIR_LEFT;
    } else if (TLN_GetInput(INPUT_RIGHT)) {
      inp.dir = DIR_RIGHT;
    }
    if (!whip_is_active() && (int)TLN_GetInput(INPUT_A)) {
      inp.jump = true;
    }
  }

  if (!TLN_GetInput(INPUT_A)) {
    jump_was_released = true;
  }
  return inp;
}

/* Arc-type commitment: deferred until the earliest frame the button *could*
 * have been released to distinguish all three hold durations.
 * Frame 1: if INPUT_A already released → short arc (tap).
 * Frame 2: if INPUT_A still held → higher arc (3+ frames); released → tall. */
static void commit_jump_arc(void) {
  if (state != SIMON_JUMPING || (int)jump_arc_committed) {
    return;
  }
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

/* If Simon is on the ground but there is no solid tile, sandblock, or
 * bridge beneath his feet, start falling. */
static void begin_edge_fall(void) {
  if (state == SIMON_JUMPING || (int)has_ground_support()) {
    return;
  }
  state = SIMON_JUMPING;
  jump_frame = JUMP_ARC_LEN_HIGHER; /* past all arcs — fall immediately */
  jump_arc_committed = true;
  jump_arc_higher = false;
  jump_arc_tall = false;
  jump_was_released = true;
  y_velocity = 0;
  ceiling_hang = 0;
  ceiling_fall_frame = -1;
  edge_fall_frame = 0;
}

/* Crouch / uncrouch / crouch-whip transitions. */
static void update_crouch_state(bool crouch_held, bool on_ground) {
  if ((int)crouch_held && (int)on_ground && !whip_is_active()) {
    if (state == SIMON_WALKING) {
      simon_set_state(SIMON_CROUCH_WALKING);
    } else if (state != SIMON_CROUCHING && state != SIMON_CROUCH_WALKING && state != SIMON_CROUCH_WHIPPING) {
      simon_set_state(SIMON_CROUCHING);
    }
  } else if (state == SIMON_CROUCH_WHIPPING) {
    if (!whip_is_active()) {
      simon_set_state((int)crouch_held ? SIMON_CROUCHING : SIMON_IDLE);
    }
  } else if ((state == SIMON_CROUCHING || state == SIMON_CROUCH_WALKING) && (!crouch_held || !on_ground)) {
    simon_set_state(state == SIMON_CROUCH_WALKING ? SIMON_WALKING : SIMON_IDLE);
  }

  if ((int)whip_is_active() && (int)on_ground && (state == SIMON_CROUCHING || state == SIMON_CROUCH_WALKING)) {
    simon_set_state(SIMON_CROUCH_WHIPPING);
  }
}

/* Teeter: idle on the bridge surface with no player input. */
static void update_teeter_state(Direction input) {
  if (state == SIMON_IDLE && bridge_floor != BRIDGE_FLOOR_Y && input == DIR_NONE) {
    simon_set_state(SIMON_TEETER);
  } else if (state == SIMON_TEETER && bridge_floor == BRIDGE_FLOOR_Y) {
    simon_set_state(SIMON_IDLE);
  }
}

void simon_tasks(void) {
  sb_count = sandblock_snapshot(sb_cache);

  SimonInput inp = read_input();
  int desired_dx = apply_movement(inp.dir);

  if ((int)inp.jump && (int)jump_was_released && state != SIMON_JUMPING) {
    simon_set_state(SIMON_JUMPING);
  }

  commit_jump_arc();

  apply_collisions(desired_dx);

  begin_edge_fall();

  /* If collisions landed Simon into IDLE but a direction is still held,
   * promote immediately to WALKING so the idle sprite never shows for one
   * frame. */
  if (state == SIMON_IDLE && inp.dir != DIR_NONE) {
    simon_set_state(SIMON_WALKING);
  }

  bool on_ground = (bool)(state != SIMON_JUMPING);
  update_crouch_state(inp.crouch_held, on_ground);
  update_teeter_state(inp.dir);

  if (state == SIMON_WALKING || state == SIMON_CROUCH_WALKING) {
    walk_anim_frame++;
  }
  render_current_state();
}

int simon_get_position(void) { return position.scroll_x; }

void simon_set_position(Coords2d pos) {
  position.x = pos.x;
  position.y = pos.y;
  position.scroll_x = 0;
  render_current_state();
}

void simon_push_right(int pixels) {
  position.x += pixels;
  /* clamp to one sprite-width past the right screen edge */
  if (position.x > TLN_GetWidth()) {
    position.x = TLN_GetWidth();
  }
  render_current_state();
}

int simon_get_screen_x(void) { return position.x; }

void simon_set_screen_x(int screen_x) {
  position.x = screen_x;
  render_current_state();
}

void simon_pin_feet_y(int feet_y) {
  /* Position-only correction: does not change state.
   * Zeroes y_velocity and resets apex_hang so advance_gravity cannot accumulate
   * velocity on surfaces without collision tiles (e.g. the drawbridge deck). */
  position.y = feet_y - SIMON_HEIGHT;
  y_velocity = 0;
  apex_hang = 0;
  render_current_state();
}

void simon_set_feet_y(int feet_y) {
  simon_pin_feet_y(feet_y);

  /* Pinning feet to a surface counts as landing: cancel any in-progress jump
   * so the jump sprite clears and the player can jump again next frame. */
  if (state == SIMON_JUMPING) {
    simon_set_state(SIMON_IDLE);
  }
}

int simon_get_feet_y(void) { return position.y + SIMON_HEIGHT; }

void simon_set_world_x(int new_world_x) { position.scroll_x = new_world_x; }

int simon_get_screen_y(void) { return position.y; }

bool simon_is_crouching(void) {
  return (bool)(state == SIMON_CROUCHING || state == SIMON_CROUCH_WALKING || state == SIMON_CROUCH_WHIPPING);
}

bool simon_is_jumping(void) { return (bool)(state == SIMON_JUMPING); }

bool simon_facing_right(void) { return (bool)(direction == DIR_RIGHT); }

void simon_warp(int world_x, int world_y) {
  int width = TLN_GetWidth();
  /* Place camera so Simon appears centred, clamped to map bounds. */
  int scroll = world_x - (width / 2);
  if (scroll < 0) {
    scroll = 0;
  } else if (scroll > layer_width - width) {
    scroll = layer_width - width;
  }
  position.scroll_x = scroll;
  position.x = world_x - scroll;
  position.y = world_y;
  y_velocity = 0;
  apex_hang = 0;
  ceiling_hang = 0;
  ceiling_fall_frame = -1;
  edge_fall_frame = -1;
  jump_frame = 0;
  simon_set_state(SIMON_IDLE);
  render_current_state();
}
