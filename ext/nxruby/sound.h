#pragma once
#include <ruby.h>
#include <SDL2/SDL_mixer.h>

// Rubyの Sound オブジェクトが持つデータ
typedef struct {
    Mix_Chunk *chunk;
    int loop_count;
} NxSound;

// 初期化と終了処理
void nx_sound_init(void);
void nx_sound_cleanup(void);