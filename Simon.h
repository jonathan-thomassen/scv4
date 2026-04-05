#ifndef SIMON_H
#define SIMON_H

#include "Prop.h"
#include "Sandblock.h"
#include "Torch.h"

typedef enum {
  SIMON_IDLE,
  SIMON_WALKING,
  SIMON_JUMPING,
  SIMON_TEETER,
  SIMON_CROUCHING,
  SIMON_CROUCH_WALKING,
  SIMON_CROUCH_WHIPPING
} SimonState;

typedef struct {
  int x;
  int y;
  int scroll_x;
} Coords2d;

#define COLLISION_LAYER 5

/* Simon occupies the last sprite slot so he renders on top of all props. */
/* Simon occupies MAX_SIMON_SPRITES consecutive sprite slots. */
#define MAX_SIMON_SPRITES 8
#define SIMON_SPRITE_BASE (1 + MAX_SANDBLOCKS + MAX_TORCHES + MAX_PROPS)

void SimonInit(void);
void SimonDeinit(void);
void SimonTasks(void);
int SimonGetPosition(void);
void SimonSetState(SimonState new_state);
void SimonSetPosition(Coords2d pos);

/* Re-inserts Simon at the end of Tilengine's sprite render list so he
 * always draws on top of torches and props that were spawned after init. */
void SimonBringToFront(void);

/**
 * Freezes the camera: after this call move_left/move_right only update the
 * screen x, never xworld. SimonGetPosition() continues to return the locked
 * scroll value so all layer positions stay fixed automatically.
 */
void SimonFreezeCamera(void);

/**
 * Pushes Simon rightward by \p pixels on screen, clamped to the screen edge.
 * Used by the drawbridge animation to carry Simon off screen as the bridge rises.
 */
void SimonPushRight(int pixels);

/** Returns Simon's current screen x position. */
int SimonGetScreenX(void);

/** Sets Simon's screen x position without affecting the world scroll offset. */
void SimonSetScreenX(int screen_x);

/**
 * Sets Simon's y so that his feet land on the given screen y coordinate.
 * Zeroes vertical velocity and cancels any jump — use for landing events.
 */
void SimonSetFeetY(int feet_y);

/**
 * Moves Simon's y so his feet are at feet_y and zeroes fall velocity,
 * without touching state or apex_hang.
 */
void SimonPinFeetY(int feet_y);

/** Returns the screen y coordinate of Simon's feet (bottom of sprite). */
int SimonGetFeetY(void);

/** Returns the screen y coordinate of Simon's top edge (top of sprite). */
int SimonGetScreenY(void);

/** Returns true if Simon is currently facing right, false if facing left. */
bool SimonFacingRight(void);

/** Returns true if Simon is in any crouching state. */
bool SimonIsCrouching(void);

/**
 * Sets a bridge-surface floor override used by SimonTasks() physics.
 * While active, the tile-based floor check is replaced by a hard clamp at
 * feet_y — call this BEFORE SimonTasks() every frame Simon is on the bridge.
 * Clears automatically each frame; re-set each frame while on the bridge.
 */
void SimonSetBridgeFloor(int feet_y);

/** Removes the bridge floor override (restores tile-based collision). */
void SimonClearBridgeFloor(void);

/**
 * Sets the bridge snap tolerance (0–8 px). When non-zero, Simon can stand
 * up to \p tol pixels into the bridge surface. Scale with bridge progress:
 * 0 when flat, 8 when fully raised. Reset to 0 by SimonClearBridgeFloor().
 */
void SimonSetBridgeTolerance(int tol);

/**
 * Directly sets the internal world-scroll offset used for tile collision
 * queries. Call once per frame during forced-scroll sequences (e.g. rails)
 * after advancing the camera position so that collision queries stay in sync
 * with the layer scroll position.
 */
void SimonSetWorldX(int world_x);

#endif