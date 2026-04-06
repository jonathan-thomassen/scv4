#include "Profiling.h"

#include <stdio.h>

#include "../src/Draw.h" /* g_prof_linebuf/fillmask/blit_ticks */

#define PROF_REPORT_INTERVAL_MS 1000
#define USEC_PER_SEC            1000000

void prof_init(ProfState* prof) {
  prof->freq = SDL_GetPerformanceFrequency();
  prof->acc_logic = 0;
  prof->acc_render = 0;
  prof->acc_frame = 0;
  prof->acc_linebuf = 0;
  prof->acc_fillmask = 0;
  prof->acc_blit = 0;
  prof->acc_layers = 0;
  prof->acc_sprites = 0;
  for (int i = 0; i < NUM_LAYERS; i++) {
    prof->acc_per_layer[i] = 0;
  }
  prof->samples = 0;
  prof->report_t = SDL_GetTicks();
}

void prof_frame_begin(ProfState* prof) { prof->frame_start = SDL_GetPerformanceCounter(); }

void prof_logic_end(ProfState* prof) { prof->logic_end = SDL_GetPerformanceCounter(); }

/* Call after TLN_DrawFrame. Prints a report once per second. */
void prof_frame_end(ProfState* prof, int xpos) {
  Uint64 now = SDL_GetPerformanceCounter();
  Uint64 t_logic = prof->logic_end - prof->frame_start;
  Uint64 t_render = now - prof->logic_end;
  Uint64 t_frame = now - prof->frame_start;
  prof->acc_logic += t_logic;
  prof->acc_render += t_render;
  prof->acc_frame += t_frame;
  /* snapshot Draw.c sub-counters then reset them for the next frame */
  prof->acc_linebuf += g_prof_linebuf_ticks;
  prof->acc_fillmask += g_prof_fillmask_ticks;
  prof->acc_blit += g_prof_blit_ticks;
  prof->acc_layers += g_prof_layers_ticks;
  prof->acc_sprites += g_prof_sprites_ticks;
  for (int i = 0; i < NUM_LAYERS; i++) {
    prof->acc_per_layer[i] += g_prof_per_layer_ticks[i];
  }
  g_prof_linebuf_ticks = 0;
  g_prof_fillmask_ticks = 0;
  g_prof_blit_ticks = 0;
  g_prof_layers_ticks = 0;
  g_prof_sprites_ticks = 0;
  for (int i = 0; i < NUM_LAYERS; i++) {
    g_prof_per_layer_ticks[i] = 0;
  }
  prof->samples++;

  Uint64 wall = SDL_GetTicks();
  if (wall - prof->report_t >= PROF_REPORT_INTERVAL_MS && prof->samples > 0) {
    Uint64 samples = (Uint64)prof->samples;
    Uint64 us_render = prof->acc_render * USEC_PER_SEC / prof->freq / samples;
    Uint64 us_layers = prof->acc_layers * USEC_PER_SEC / prof->freq / samples;
    Uint64 us_linebuf = prof->acc_linebuf * USEC_PER_SEC / prof->freq / samples;
    Uint64 us_fillmask = prof->acc_fillmask * USEC_PER_SEC / prof->freq / samples;
    Uint64 us_blit = prof->acc_blit * USEC_PER_SEC / prof->freq / samples;
    Uint64 us_sprites = prof->acc_sprites * USEC_PER_SEC / prof->freq / samples;
    /* everything inside TLN_DrawFrame that isn't layer/sprite CPU work */
    Uint64 us_cpu = us_layers + us_sprites;
    Uint64 us_sdl = us_render > us_cpu ? us_render - us_cpu : 0;
    fprintf(stderr, "PROF xpos=%4d  render=%5llu us  layers=%5llu  sprites=%4llu  sdl=%5llu  [", xpos,
            (unsigned long long)us_render, (unsigned long long)us_layers, (unsigned long long)us_sprites,
            (unsigned long long)us_sdl);
    for (int i = 0; i < NUM_LAYERS; i++) {
      fprintf(stderr, "L%d=%4llu%s", i,
              (unsigned long long)(prof->acc_per_layer[i] * USEC_PER_SEC / prof->freq / samples),
              i < NUM_LAYERS - 1 ? "  " : "]\n");
    }
    fprintf(stderr, "      L1 sub: linebuf=%4llu us  fillmask=%4llu us  blit=%4llu us\n",
            (unsigned long long)us_linebuf, (unsigned long long)us_fillmask, (unsigned long long)us_blit);
    prof->acc_logic = 0;
    prof->acc_render = 0;
    prof->acc_frame = 0;
    prof->acc_linebuf = 0;
    prof->acc_fillmask = 0;
    prof->acc_blit = 0;
    prof->acc_layers = 0;
    prof->acc_sprites = 0;
    for (int i = 0; i < NUM_LAYERS; i++) {
      prof->acc_per_layer[i] = 0;
    }
    prof->samples = 0;
    prof->report_t = wall;
  }
}
