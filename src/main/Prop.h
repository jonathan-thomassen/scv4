#ifndef PROP_H
#define PROP_H

/** Maximum number of prop instances that can exist simultaneously. */
#define MAX_PROPS      16

/**
 * Maximum number of distinct spritesets that props can share.
 * Multiple props with the same name load the spriteset only once.
 */
#define MAX_PROP_TYPES 8

/** Initialises the prop system. Call once before PropSpawn(). */
void prop_init(void);

/** Frees all prop spritesets and disables all prop sprites. */
void prop_deinit(void);

/**
 * Spawns a static prop at the given world position.
 *
 * The spriteset is looked up by \p name (case-insensitive). If the same name
 * has been used before its spriteset is reused without reloading.
 *
 * \param name      Spriteset filename without extension (e.g. "pillar")
 * \param world_x   World x position (pixels from map origin, left edge)
 * \param world_y   World y position (pixels from map origin, top edge)
 * \return          Slot index on success, -1 if no free slot or load failed
 */
int prop_spawn(const char* name, int world_x, int world_y);

/**
 * Spawns a screen-fixed background prop rendered behind all tilemap layers.
 *
 * The prop is positioned at \p screen_x / \p screen_y in screen space and
 * never re-positioned during \c PropTasks(), so it does not scroll.
 * It is drawn before the first tilemap layer, appearing behind everything.
 *
 * \param name      Spriteset filename without extension (e.g. "moon")
 * \param screen_x  Fixed screen x position (pixels from left edge)
 * \param screen_y  Fixed screen y position (pixels from top edge)
 * \return          Slot index on success, -1 if no free slot or load failed
 */
int prop_spawn_background(const char* name, int screen_x, int screen_y);

/**
 * Repositions all active prop sprites to match the current scroll offset.
 * Call once per frame before TLN_DrawFrame().
 *
 * \param xworld  Current horizontal world scroll offset
 */
void prop_tasks(int xworld);

/**
 * Overrides the world position of a spawned prop.
 *
 * \param idx      Slot index returned by PropSpawn()
 * \param world_x  New world x position
 * \param world_y  New world y position
 */
void prop_set_world_pos(int idx, int world_x, int world_y);

/**
 * Re-inserts a prop at the tail of Tilengine's sprite render list so it
 * draws on top of sprites that were added before it.
 *
 * \param idx  Slot index returned by PropSpawn()
 */
void prop_bring_to_front(int idx);

/**
 * Sets or clears FLAG_PRIORITY on a prop sprite so it renders after priority
 * tile pixels are composited (i.e. in front of priority tiles).
 *
 * \param idx     Slot index returned by PropSpawn()
 * \param enable  true to enable priority rendering, false to disable
 */
void prop_set_priority(int idx, bool enable);

/**
 * Enables or disables per-pixel blend-mask rendering on a prop sprite.
 * When enabled the sprite is composited through the engine's blend mask so
 * pixels that sit over an opaque water (mask) pixel are blended 50/50 with
 * whatever is already in the framebuffer.
 *
 * \param idx     Slot index returned by PropSpawn()
 * \param enable  true to enable blend-mask compositing, false to disable
 */
void prop_enable_blend_mask(int idx, bool enable);

#endif
