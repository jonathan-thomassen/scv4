#ifndef TORCH_H
#define TORCH_H

/** Maximum number of torches that can exist simultaneously. */
#define MAX_TORCHES 8

/**
 * Loads the torch spriteset and clears all slots.
 * Must be called once before TorchSpawn().
 */
void TorchInit(void);

/** Frees all torch resources. */
void TorchDeinit(void);

/**
 * Activates a torch at the given world coordinates.
 *
 * \param world_x  World x position (pixels from map origin)
 * \param world_y  World y position (top of torches, pixels from map origin)
 * \return         Slot index (0..MAX_TORCHES-1) on success, -1 if full
 */
int TorchSpawn(int world_x, int world_y);

/**
 * Updates screen positions of all active torches.
 * Call once per frame before TLN_DrawFrame().
 *
 * \param xworld  Current horizontal world scroll offset
 */
void TorchTasks(int xworld);

/** Pixel dimensions of one torch — needed for AABB tests in Simon.c. */
#define TORCH_WIDTH 16
#define TORCH_HEIGHT 16

#endif
