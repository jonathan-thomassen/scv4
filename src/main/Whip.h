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
#define WHIP_DURATION    23

/**
 * Loads the whip0 spriteset and clears the sprite slot.
 * Must be called after SimonInit() so that all earlier sprite slots are
 * already claimed, keeping render order correct.
 */
void whip_init(void);

/** Frees whip resources and disables the sprite slot. */
void whip_deinit(void);

/**
 * Returns true while a whip swing is in progress.
 * Simon's movement input should be suppressed whenever this is true.
 */
bool whip_is_active(void);

/**
 * Returns the current whip animation stage (0-based) while a swing is active,
 * or 0 when inactive.  Used by Simon.c to synchronise his body pose.
 */
int whip_get_stage(void);

/**
 * Returns true if the current (or most recently started) swing was triggered
 * with INPUT_UP held — i.e. Simon is whipping upward.
 */
bool whip_is_up(void);

/**
 * Returns true if the current swing was triggered with INPUT_DOWN held while
 * airborne — i.e. Simon is whipping downward.
 */
bool whip_is_down(void);

/**
 * Polls INPUT_B (keyboard X) to start a swing, then advances the frame
 * counter and stage selection. Call once per game frame, BEFORE SimonTasks().
 * Sprite placement is deferred — call WhipRender() after SimonTasks().
 */
void whip_tasks(void);

/**
 * Places whip sprites at Simon's current (post-movement) screen position.
 * Call once per game frame, immediately after SimonTasks().
 */
void whip_render(void);

#endif /* WHIP_H */
