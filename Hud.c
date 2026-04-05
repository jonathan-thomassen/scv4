#include "Hud.h"

#include <stdint.h>

/*
 * Tile ID in the HUD tileset that represents digit '0'.
 * Digits 0-9 occupy ten consecutive tiles starting here, so digit d maps to
 * tile index (DIGIT_TILE_0 + d).  Adjust if your tileset uses a different
 * layout.
 */
#define DIGIT_TILE_0 2

/*
 * Tilemap row (0-based) that holds the three timer digit cells.
 * Adjust to match the row in hud.tmx where the timer is displayed.
 */
#define TIMER_ROW 3

/*
 * Tilemap column (0-based) of the hundreds digit.
 * The tens digit is at TIMER_COL+1, units at TIMER_COL+2.
 */
#define TIMER_COL 26

/* Number of in-game frames between each timer decrement. */
#define FRAMES_PER_TICK 60

/* Starting value of the countdown timer. */
#define TIMER_START 450

/* ------------------------------------------------------------------ */

typedef struct {
  int col;
  int digit;
} TimerCoords;

static TLN_Tilemap hud_tilemap;
static int timer_value;
static int frame_count;

/* Writes a single digit tile to the given tilemap column on TIMER_ROW. */
static void write_digit(TimerCoords coords) {
  union Tile tile = {0};
  tile.index = (uint16_t)(DIGIT_TILE_0 + coords.digit);
  TLN_SetTilemapTile(hud_tilemap, TIMER_ROW, coords.col, &tile);
}

/* Decomposes timer_value into three digits and writes them to the tilemap. */
static void update_display(void) {
  write_digit((TimerCoords){TIMER_COL, timer_value / 100});
  write_digit((TimerCoords){TIMER_COL + 1, (timer_value / 10) % 10});
  write_digit((TimerCoords){TIMER_COL + 2, timer_value % 10});
}

void HudInit(TLN_Tilemap tilemap) {
  hud_tilemap = tilemap;
  timer_value = TIMER_START;
  frame_count = 0;
  update_display();
}

void HudTasks(void) {
  if (timer_value <= 0) {
    return;
  }
  frame_count++;
  if (frame_count < FRAMES_PER_TICK) {
    return;
  }
  frame_count = 0;
  timer_value--;
  update_display();
}
