#pragma once
#include <ruby.h>
#include <stdbool.h>
#include <SDL3/SDL.h>

// モジュール登録用の初期化関数
void nx_input_init(void);

// 毎フレーム呼ぶ更新関数
void nx_input_handle_event(const SDL_Event *event);
void nx_input_update(void);