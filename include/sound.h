#pragma once
#include <mruby.h>
#include <SDL3_mixer/SDL_mixer.h>

typedef struct {
    MIX_Audio *audio;
    MIX_Track *track;   
    int loop_count;
} NxSound;

void nx_sound_init(mrb_state *mrb);
void nx_sound_cleanup(void);