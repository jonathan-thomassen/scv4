#ifndef HUD_H
#define HUD_H

#include "Tilengine.h"

/**
 * Initialises the HUD timer system, writes the initial value (450) to the
 * tilemap, and stores the tilemap reference for subsequent updates.
 *
 * \param tilemap  The loaded HUD tilemap (from TLN_LoadTilemap /
 * TLN_GetLayerTilemap).
 */
void HudInit(TLN_Tilemap tilemap);

/**
 * Updates the HUD each frame.  Decrements the timer by 1 every 60 frames
 * and rewrites the digit tiles in the tilemap accordingly.
 * Call once per frame before TLN_DrawFrame().
 */
void HudTasks(void);

#endif
