#pragma once
#include <ruby.h>
#include <SDL3_mixer/SDL_mixer.h>

// Rubyの Sound オブジェクトが持つデータ (そのまま維持)
typedef struct {
    MIX_Audio *audio;
    MIX_Track *track;   
    int loop_count;
} NxSound;

// 初期化と終了処理
void nx_sound_init(void);
void nx_sound_cleanup(void);