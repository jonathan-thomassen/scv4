#include "Profiling.h"

#include <stdio.h>

#include "../src/Draw.h" /* g_prof_linebuf/fillmask/blit_ticks */

void prof_init(ProfState *p) {
  p->freq = SDL_GetPerformanceFrequency();
  p->acc_logic = 0;
  p->acc_render = 0;
  p->acc_frame = 0;
  p->acc_linebuf = 0;
  p->acc_fillmask = 0;
  p->acc_blit = 0;
  p->acc_layers = 0;
  p->acc_sprites = 0;
  for (int i = 0; i < NUM_LAYERS; i++) {
    p->acc_per_layer[i] = 0;
  }
  p->samples = 0;
  p->report_t = SDL_GetTicks();
}

void prof_frame_begin(ProfState *p) { p->frame_start = SDL_GetPerformanceCounter(); }

void prof_logic_end(ProfState *p) { p->logic_end = SDL_GetPerformanceCounter(); }

/* Call after TLN_DrawFrame. Prints a report once per second. */
void prof_frame_end(ProfState *p, int xpos) {
  Uint64 now = SDL_GetPerformanceCounter();
  Uint64 t_logic = p->logic_end - p->frame_start;
  Uint64 t_render = now - p->logic_end;
  Uint64 t_frame = now - p->frame_start;
  p->acc_logic += t_logic;
  p->acc_render += t_render;
  p->acc_frame += t_frame;
  /* snapshot Draw.c sub-counters then reset them for the next frame */
  p->acc_linebuf += g_prof_linebuf_ticks;
  p->acc_fillmask += g_prof_fillmask_ticks;
  p->acc_blit += g_prof_blit_ticks;
  p->acc_layers += g_prof_layers_ticks;
  p->acc_sprites += g_prof_sprites_ticks;
  for (int i = 0; i < 7; i++) {
    p->acc_per_layer[i] += g_prof_per_layer_ticks[i];
  }
  g_prof_linebuf_ticks = 0;
  g_prof_fillmask_ticks = 0;
  g_prof_blit_ticks = 0;
  g_prof_layers_ticks = 0;
  g_prof_sprites_ticks = 0;
  for (int i = 0; i < 7; i++) {
    g_prof_per_layer_ticks[i] = 0;
  }
  p->samples++;

  Uint64 wall = SDL_GetTicks();
  if (wall - p->report_t >= 1000 && p->samples > 0) {
    Uint64 s = (Uint64)p->samples;
    Uint64 us_render = p->acc_render * 1000000 / p->freq / s;
    Uint64 us_layers = p->acc_layers * 1000000 / p->freq / s;
    Uint64 us_linebuf = p->acc_linebuf * 1000000 / p->freq / s;
    Uint64 us_fillmask = p->acc_fillmask * 1000000 / p->freq / s;
    Uint64 us_blit = p->acc_blit * 1000000 / p->freq / s;
    Uint64 us_sprites = p->acc_sprites * 1000000 / p->freq / s;
    /* everything inside TLN_DrawFrame that isn't layer/sprite CPU work */
    Uint64 us_cpu = us_layers + us_sprites;
    Uint64 us_sdl = us_render > us_cpu ? us_render - us_cpu : 0;
    fprintf(stderr, "PROF xpos=%4d  render=%5llu us  layers=%5llu  sprites=%4llu  sdl=%5llu  [",
            xpos, (unsigned long long)us_render, (unsigned long long)us_layers,
            (unsigned long long)us_sprites, (unsigned long long)us_sdl);
    for (int i = 0; i < 7; i++) {
      fprintf(stderr, "L%d=%4llu%s", i,
              (unsigned long long)(p->acc_per_layer[i] * 1000000 / p->freq / s),
              i < 6 ? "  " : "]\n");
    }
    fprintf(stderr, "      L1 sub: linebuf=%4llu us  fillmask=%4llu us  blit=%4llu us\n",
            (unsigned long long)us_linebuf, (unsigned long long)us_fillmask,
            (unsigned long long)us_blit);
    p->acc_logic = 0;
    p->acc_render = 0;
    p->acc_frame = 0;
    p->acc_linebuf = 0;
    p->acc_fillmask = 0;
    p->acc_blit = 0;
    p->acc_layers = 0;
    p->acc_sprites = 0;
    for (int i = 0; i < 7; i++) {
      p->acc_per_layer[i] = 0;
    }
    p->samples = 0;
    p->report_t = wall;
  }
}
