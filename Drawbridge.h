#ifndef DRAWBRIDGE_H
#define DRAWBRIDGE_H

#include <stdbool.h>

#define DB_STEPS 135
#define DB_TICK_RATE 9

typedef struct {
  int x;
  int y;
} ChainPos;

void DrawbridgeInit(int layer, int hinge_x, int hinge_y);
/** Advance the animation by one step. Does nothing when already at the last frame. */
void DrawbridgeAdvance(void);
int DrawbridgeGetProgress(void);
void DrawbridgeSetHinge(int hinge_x, int hinge_y);
void DrawbridgeTasks(void);

/**
 * Advance the drawbridge tick counter.
 * Returns true once every 9 calls, providing
 * a 9-frame rate divider for the animation progress.
 * Must be called exactly once per game frame while the animation is active.
 */
bool DrawbridgeTick(void);

/**
 * Returns the screen y-coordinate of the bridge surface at the given screen x.
 * Uses the hinge position and current progress to compute the rotated height.
 * Returns hinge_y when progress is 0 (bridge flat).
 */
int DrawbridgeSurfaceY(int screen_x);

/** Returns the screen x of the bridge hinge (the fixed right-side anchor). */
int DrawbridgeHingeX(void);

/**
 * Returns the precomputed screen position of the chain-sprite anchor for the
 * current animation step.  Add xpos to .x to get the world x coordinate.
 * .y is the fully-baked world y (sprite height and drift already folded in).
 */
ChainPos DrawbridgeChainPos(void);

#endif
