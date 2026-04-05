#include "Torch.h"

#include <stdbool.h>
#include <stdlib.h>

#include "Sandblock.h" /* for MAX_SANDBLOCKS */
#include "Tilengine.h"

/* Torch slots follow sandblocks: 1..MAX_SANDBLOCKS are sandblocks,
 * 1+MAX_SANDBLOCKS..1+MAX_SANDBLOCKS+MAX_TORCHES-1 are torches. */
#define SPRITE_BASE (1 + MAX_SANDBLOCKS)

/* Torches scrolled more than this many pixels past the left screen edge are
 * permanently deactivated; chosen to exceed the widest possible torch sprite. */
#define OFFSCREEN_CULL_MARGIN 64

typedef struct {
  bool active;
  int world_x;
  int world_y;
} Torch;

static TLN_Spriteset spriteset;
static TLN_SequencePack seq_pack;
static Torch torches[MAX_TORCHES];

void TorchInit(void) {
  spriteset = TLN_LoadSpriteset("torch");
  seq_pack = TLN_LoadSequencePack("torch.sqx");
  for (int i = 0; i < MAX_TORCHES; i++) {
    torches[i].active = false;
    TLN_DisableSprite(SPRITE_BASE + i);
  }
}

void TorchDeinit(void) {
  for (int i = 0; i < MAX_TORCHES; i++) {
    TLN_DisableSprite(SPRITE_BASE + i);
  }
  if (seq_pack != NULL) {
    TLN_DeleteSequencePack(seq_pack);
  }
  seq_pack = NULL;
  if (spriteset != NULL) {
    TLN_DeleteSpriteset(spriteset);
  }
  spriteset = NULL;
}

int TorchSpawn(int world_x, int world_y) {
  for (int i = 0; i < MAX_TORCHES; i++) {
    if (torches[i].active) {
      continue;
    }
    torches[i].active = true;
    torches[i].world_x = world_x;
    torches[i].world_y = world_y;
    TLN_SetSpriteSet(SPRITE_BASE + i, spriteset);
    if (seq_pack != NULL) {
      TLN_SetSpriteAnimation(SPRITE_BASE + i, TLN_GetSequence(seq_pack, 0), 0);
    } else {
      TLN_SetSpritePicture(SPRITE_BASE + i, 0);
    }
    return i;
  }
  return -1; /* no free slot */
}

void TorchTasks(int xworld) {
  for (int i = 0; i < MAX_TORCHES; i++) {
    if (!torches[i].active) {
      continue;
    }
    int screen_x = torches[i].world_x - xworld;
    if (screen_x < -OFFSCREEN_CULL_MARGIN) {
      /* Scrolled permanently off the left edge; free the sprite slot. */
      torches[i].active = false;
      TLN_DisableSprite(SPRITE_BASE + i);
      continue;
    }
    TLN_SetSpritePosition(SPRITE_BASE + i, screen_x, torches[i].world_y);
  }
}
