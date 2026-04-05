#pragma once

#include <SDL3/SDL_timer.h>

#define NUM_LAYERS 7

typedef struct {
  Uint64 freq;
  Uint64 frame_start;
  Uint64 logic_end;
  Uint64 acc_logic;
  Uint64 acc_render;
  Uint64 acc_frame;
  Uint64 acc_linebuf;
  Uint64 acc_fillmask;
  Uint64 acc_blit;
  Uint64 acc_layers;
  Uint64 acc_sprites;
  Uint64 acc_per_layer[NUM_LAYERS];
  int samples;
  Uint64 report_t;
} ProfState;

void prof_init(ProfState *p);
void prof_frame_begin(ProfState *p);
void prof_logic_end(ProfState *p);
void prof_frame_end(ProfState *p, int xpos);
