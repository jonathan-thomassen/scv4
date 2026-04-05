#include "Prop.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "Sandblock.h" /* for MAX_SANDBLOCKS — defines where our slots begin */
#include "Sprite.h"
#include "Tilengine.h"
#include "Torch.h" /* for MAX_TORCHES */

/* Prop sprite slots follow sandblocks and torches. */
#define SPRITE_BASE (1 + MAX_SANDBLOCKS + MAX_TORCHES)

/* Props scrolled more than this many pixels past the left screen edge are
 * permanently deactivated; chosen to exceed the widest possible prop sprite. */
#define OFFSCREEN_CULL_MARGIN 64

/* A loaded spriteset shared by all props of the same name. */
typedef struct {
  char name[32];
  TLN_Spriteset ss;
  TLN_SequencePack sp; /* NULL if no matching .sqx file exists */
} PropType;

typedef struct {
  bool active;
  bool fixed;   /* screen-fixed background prop (FLAG_BACKGROUND, no scroll) */
  int type_idx; /* index into types[] */
  int world_x;
  int world_y;
} Prop;

static PropType types[MAX_PROP_TYPES];
static int num_types;
static Prop props[MAX_PROPS];

/* Returns the type index for name, loading the spriteset if not seen before.
 * Returns -1 if the spriteset could not be loaded or the type table is full. */
static int find_or_load_type(const char *name) {
  for (int i = 0; i < num_types; i++) {
    if (!strcasecmp(types[i].name, name)) {
      return i;
    }
  }
  if (num_types >= MAX_PROP_TYPES) {
    return -1;
  }
  TLN_Spriteset spriteset = TLN_LoadSpriteset(name);
  if (spriteset == NULL) {
    return -1;
  }
  types[num_types].ss = spriteset;
  /* Try to load a same-named sequence pack; silently skip if absent. */
  char sqx_name[36];
  snprintf(sqx_name, sizeof(sqx_name), "%s.sqx", name);
  types[num_types].sp = TLN_LoadSequencePack(sqx_name);
  strncpy(types[num_types].name, name, sizeof(types[num_types].name) - 1);
  types[num_types].name[sizeof(types[num_types].name) - 1] = '\0';
  return num_types++;
}

void PropInit(void) {
  num_types = 0;
  for (int i = 0; i < MAX_PROPS; i++) {
    props[i].active = false;
    props[i].fixed = false;
    TLN_DisableSprite(SPRITE_BASE + i);
  }
}

void PropDeinit(void) {
  for (int i = 0; i < MAX_PROPS; i++) {
    props[i].active = false;
    TLN_DisableSprite(SPRITE_BASE + i);
  }
  for (int i = 0; i < num_types; i++) {
    if (types[i].ss != NULL) {
      TLN_DeleteSpriteset(types[i].ss);
    }
    if (types[i].sp != NULL) {
      TLN_DeleteSequencePack(types[i].sp);
    }
    types[i].ss = NULL;
    types[i].sp = NULL;
  }
  num_types = 0;
}

int PropSpawn(const char *name, int world_x, int world_y) {
  int type_idx = find_or_load_type(name);
  if (type_idx < 0) {
    return -1;
  }

  for (int i = 0; i < MAX_PROPS; i++) {
    if (props[i].active) {
      continue;
    }
    props[i].active = true;
    props[i].fixed = false;
    props[i].type_idx = type_idx;
    props[i].world_x = world_x;
    props[i].world_y = world_y;
    TLN_SetSpriteSet(SPRITE_BASE + i, types[type_idx].ss);
    TLN_SetSpritePicture(SPRITE_BASE + i, 0);
    if (types[type_idx].sp != NULL) {
      TLN_SetSpriteAnimation(SPRITE_BASE + i, TLN_GetSequence(types[type_idx].sp, 0), 0);
    }
    return i;
  }
  return -1; /* no free slot */
}

int PropSpawnBackground(const char *name, int screen_x, int screen_y) {
  int type_idx = find_or_load_type(name);
  if (type_idx < 0) {
    return -1;
  }

  for (int i = 0; i < MAX_PROPS; i++) {
    if (props[i].active) {
      continue;
    }
    props[i].active = true;
    props[i].fixed = true;
    props[i].type_idx = type_idx;
    props[i].world_x = screen_x;
    props[i].world_y = screen_y;
    int slot = SPRITE_BASE + i;
    TLN_SetSpriteSet(slot, types[type_idx].ss);
    TLN_SetSpritePicture(slot, 0);
    if (types[type_idx].sp != NULL) {
      TLN_SetSpriteAnimation(slot, TLN_GetSequence(types[type_idx].sp, 0), 0);
    }
    /* render behind all tilemap layers */
    TLN_EnableSpriteFlag(slot, FLAG_BACKGROUND, true);
    /* position once — stays fixed on screen */
    TLN_SetSpritePosition(slot, screen_x, screen_y);
    return i;
  }
  return -1; /* no free slot */
}

void PropTasks(int xworld) {
  for (int i = 0; i < MAX_PROPS; i++) {
    if (!props[i].active) {
      continue;
    }
    if (props[i].fixed) {
      continue; /* screen-fixed: position never changes after spawn */
    }
    int screen_x = props[i].world_x - xworld;
    if (screen_x < -OFFSCREEN_CULL_MARGIN) {
      /* Scrolled permanently off the left edge; free the sprite slot. */
      props[i].active = false;
      TLN_DisableSprite(SPRITE_BASE + i);
      continue;
    }
    TLN_SetSpritePosition(SPRITE_BASE + i, screen_x, props[i].world_y);
  }
}

void PropSetWorldPos(int idx, int world_x, int world_y) {
  if (idx < 0 || idx >= MAX_PROPS || !props[idx].active) {
    return;
  }
  props[idx].world_x = world_x;
  props[idx].world_y = world_y;
}

void PropEnableBlendMask(int idx, bool enable) {
  if (idx < 0 || idx >= MAX_PROPS || !props[idx].active) {
    return;
  }
  TLN_EnableSpriteFlag(SPRITE_BASE + idx, SPRITE_FLAG_BLEND_MASK, enable);
}

void PropBringToFront(int idx) {
  if (idx < 0 || idx >= MAX_PROPS || !props[idx].active) {
    return;
  }
  int slot = SPRITE_BASE + idx;
  int type_idx = props[idx].type_idx;
  /* Disable removes the sprite from the render list; re-setting the spriteset
   * re-appends it at the tail so it draws on top of all earlier sprites. */
  TLN_DisableSprite(slot);
  TLN_SetSpriteSet(slot, types[type_idx].ss);
  TLN_SetSpritePicture(slot, 0);
  if (types[type_idx].sp != NULL) {
    TLN_SetSpriteAnimation(slot, TLN_GetSequence(types[type_idx].sp, 0), 0);
  }
}

void PropSetPriority(int idx, bool enable) {
  if (idx < 0 || idx >= MAX_PROPS || !props[idx].active) {
    return;
  }
  TLN_EnableSpriteFlag(SPRITE_BASE + idx, FLAG_PRIORITY, enable);
}
