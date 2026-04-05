#ifndef SANDBLOCK_H
#define SANDBLOCK_H

/** Maximum number of sandblocks that can exist simultaneously. */
#define MAX_SANDBLOCKS 4

/**
 * Loads the sandblock spriteset and clears all slots.
 * Must be called once before SandblockSpawn().
 */
void SandblockInit(void);

/** Frees all sandblock resources. */
void SandblockDeinit(void);

/**
 * Activates a sandblock at the given world coordinates.
 *
 * \param world_x  World x position (pixels from map origin)
 * \param world_y  World y position (top of block, pixels from map origin)
 * \return         Slot index (0..MAX_SANDBLOCKS-1) on success, -1 if full
 */
int SandblockSpawn(int world_x, int world_y);

/**
 * Updates screen positions of all active sandblocks.
 * Call once per frame before TLN_DrawFrame().
 *
 * \param xworld  Current horizontal world scroll offset
 */
void SandblockTasks(int xworld);

/**
 * \param sprite_x   Simon's screen x position
 * \param world_x    Horizontal world scroll offset
 * \param inout_y    Candidate new y; snapped to block top on hit
 * \param inout_vy   Vertical velocity; zeroed on landing
 * \return           true if a sandblock floor was hit
 */

/** Read-only snapshot of a sandblock used for external collision queries. */
typedef struct {
  int index; /* slot index, for SandblockMarkStood() */
  bool falling;
  int world_x;
  int world_y;
} SandblockState;

/** Pixel dimensions of one sandblock — needed for AABB tests in Simon.c. */
#define SANDBLOCK_WIDTH 16
#define SANDBLOCK_HEIGHT 16

/**
 * Fills \p out with the state of slot \p index.
 * Returns true if the slot is active, false if empty (\p out is not written).
 */
bool SandblockGet(int index, SandblockState *out);

/**
 * Marks slot \p index as stood-on this frame so SandblockTasks() can
 * advance its crumble counter.  Call when Simon's floor check hits a block.
 */
void SandblockMarkStood(int index);

/**
 * Fills \p out with states of all active, non-falling sandblocks.
 * Returns the number of entries written (0 .. MAX_SANDBLOCKS).
 * Use this to build a per-frame snapshot and avoid re-scanning the pool
 * in every collision function.
 */
int SandblockSnapshot(SandblockState out[]);

#endif
