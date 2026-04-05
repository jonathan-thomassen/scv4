#include "GridSpriteset.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "LoadFile.h"
#include "Tilengine.h"

#define MAX_FILENAME_LEN 64
#define MAX_LINE_LEN     64
#define MAX_FLAGS_LEN    8

TLN_Spriteset load_grid_spriteset(const char* base_name, TLN_Bitmap* out_bitmap) {
  char txt_name[MAX_FILENAME_LEN];
  char png_name[MAX_FILENAME_LEN];
  snprintf(txt_name, sizeof(txt_name), "%s.txt", base_name);
  snprintf(png_name, sizeof(png_name), "%s.png", base_name);

  FILE* file = FileOpen(txt_name);
  if (file == NULL) {
    return NULL;
  }

  int tile_width = 0;
  int tile_height = 0;
  int cols = 0;

  char line[MAX_LINE_LEN];
  while (fgets(line, sizeof(line), file) != NULL) {
    int val;
    if (sscanf(line, " w = %d", &val) == 1) {
      tile_width = val;
    } else if (sscanf(line, " h = %d", &val) == 1) {
      tile_height = val;
    } else if (sscanf(line, " cols = %d", &val) == 1) {
      cols = val;
    }
  }
  fclose(file);

  if (tile_width <= 0 || tile_height <= 0 || cols <= 0) {
    return NULL;
  }

  TLN_Bitmap bmp = TLN_LoadBitmap(png_name);
  if (bmp == NULL) {
    return NULL;
  }

  int rows = TLN_GetBitmapHeight(bmp) / tile_height;
  int total = rows * cols;

  TLN_SpriteData* data = (TLN_SpriteData*)malloc((size_t)total * sizeof(TLN_SpriteData));
  if (data == NULL) {
    TLN_DeleteBitmap(bmp);
    return NULL;
  }

  for (int row = 0; row < rows; row++) {
    for (int column = 0; column < cols; column++) {
      TLN_SpriteData* sprite_data = &data[(row * cols) + column];
      snprintf(sprite_data->name, sizeof(sprite_data->name), "s%d", (row * cols) + column);
      sprite_data->x = column * tile_width;
      sprite_data->y = row * tile_height;
      sprite_data->w = tile_width;
      sprite_data->h = tile_height;
    }
  }

  TLN_Spriteset spriteset = TLN_CreateSpriteset(bmp, data, total);
  free(data);
  /* Do NOT delete bmp here — the spriteset holds a reference to it.
   * The caller is responsible for deleting it after the spriteset is freed. */
  *out_bitmap = bmp;
  return spriteset;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void load_map_section(const char* filename, const char* section, int max_stages, int max_segs, MapSection* out) {
  out->num_stages = 0;
  for (int stage = 0; stage < MAP_MAX_STAGES; stage++) {
    out->stages[stage].count = 0;
  }

  FILE* file = FileOpen(filename);
  if (file == NULL) {
    return;
  }

  bool in_section = false;
  int cur_stage = -1;
  char line[MAX_LINE_LEN * 2];
  while (fgets(line, sizeof(line), file) != NULL) {
    if (line[0] == '#') {
      char name[MAX_FILENAME_LEN] = "";
      sscanf(line, "# %63s", name);
      in_section = (strcmp(name, section) == 0);
      cur_stage = -1;
      continue;
    }
    if (!in_section) {
      continue;
    }

    int idx = -1;
    if (sscanf(line, " %d :", &idx) == 1 && idx >= 0 && idx < max_stages) {
      cur_stage = idx;
      if (cur_stage + 1 > out->num_stages) {
        out->num_stages = cur_stage + 1;
      }
      continue;
    }

    if (cur_stage < 0 || out->stages[cur_stage].count >= max_segs) {
      continue;
    }

    int pic = -1;
    int displacement_x = 0;
    int displacement_y = 0;
    char flags[MAX_FLAGS_LEN] = "";

    int matched = sscanf(line, " s%d = ( %d , %d ) %7s", &pic, &displacement_x, &displacement_y, flags);
    if (matched < 3 || pic < 0) {
      continue;
    }

    MapSeg* seg = &out->stages[cur_stage].segs[out->stages[cur_stage].count++];
    seg->pic = pic;
    seg->dx = displacement_x;
    seg->dy = displacement_y;
    seg->flip_h = (strchr(flags, 'h') != NULL);
    seg->flip_v = (strchr(flags, 'v') != NULL);
  }
  fclose(file);
}
