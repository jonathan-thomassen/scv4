#ifndef WHIP_H
#define WHIP_H

#include <stdbool.h>

#include "Simon.h" /* MAX_SIMON_SPRITES, and transitively MAX_PROPS/MAX_TORCHES/MAX_SANDBLOCKS */

/*
 * Simon occupies MAX_SIMON_SPRITES sprite slots starting at SIMON_SPRITE_BASE.
 * The whip lash occupies the next MAX_WHIP_SPRITES slots — one per chain
 * segment that can be on-screen simultaneously.
 */
#define MAX_WHIP_SPRITES 12
#define WHIP_SPRITE_BASE (1 + MAX_SANDBLOCKS + MAX_TORCHES + MAX_PROPS + MAX_SIMON_SPRITES)

/** Total duration of a whip swing, in game frames. */
#define WHIP_DURATION 18

/**
 * Loads the whip0 spriteset and clears the sprite slot.
 * Must be called after SimonInit() so that all earlier sprite slots are
 * already claimed, keeping render order correct.
 */
void WhipInit(void);

/** Frees whip resources and disables the sprite slot. */
void WhipDeinit(void);

/**
 * Returns true while a whip swing is in progress.
 * Simon's movement input should be suppressed whenever this is true.
 */
bool WhipIsActive(void);

/**
 * Returns the current whip animation stage (0-based) while a swing is active,
 * or 0 when inactive.  Used by Simon.c to synchronise his body pose.
 */
int WhipGetStage(void);

/**
 * Returns true if the current (or most recently started) swing was triggered
 * with INPUT_UP held — i.e. Simon is whipping upward.
 */
bool WhipIsUp(void);

/**
 * Polls INPUT_B (keyboard X) to start a swing, then advances the frame
 * counter and stage selection. Call once per game frame, BEFORE SimonTasks().
 * Sprite placement is deferred — call WhipRender() after SimonTasks().
 */
void WhipTasks(void);

/**
 * Places whip sprites at Simon's current (post-movement) screen position.
 * Call once per game frame, immediately after SimonTasks().
 */
void WhipRender(void);

#endif /* WHIP_H */
