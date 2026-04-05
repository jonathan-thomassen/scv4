#ifndef SIMON_COLLISION_H
#define SIMON_COLLISION_H

#define TILE_SIZE 8
#define BLOCK_SIZE 16
#define SIMON_COL_WIDTH 16
#define SIMON_COL_HEIGHT 46
#define SIMON_COL_X_OFFSET 8
#define SIMON_COL_LEFT_X(world_x, sprite_x) ((world_x) + (sprite_x) + SIMON_COL_X_OFFSET)
#define SIMON_COL_RIGHT_X(world_x, sprite_x)                                                       \
  ((world_x) + (sprite_x) + SIMON_COL_X_OFFSET + SIMON_COL_WIDTH)
#define SIMON_COL_BOTTOM_Y(sprite_y) ((sprite_y) + SIMON_COL_HEIGHT - 1)
#define MAX_VELOCITY 8

/* Load the col_definition lookup table from disk.  Must be called once
 * before any call to resolve_collision(). */
void load_col_definition(void);

/* Resolves a candidate displacement (*displacement_x, *displacement_y) against tile collision.
 * Selects the appropriate probe set for the movement direction and clamps
 * each axis to the nearest tile boundary.  Both displacement_x and displacement_y may be modified.
 *
 * \param world_x   Horizontal scroll offset (camera position)
 * \param sprite_x  Sprite screen-x position
 * \param sprite_y  Sprite screen-y position (top of bounding box)
 * \param displacement_x        In/out: candidate horizontal displacement
 * \param displacement_y        In/out: candidate vertical displacement
 */
void resolve_collision(int world_x, int sprite_x, int sprite_y, int *displacement_x,
                       int *displacement_y);

#endif
