// input.c
//
// 外部のC言語から呼ばれる関数のみを書く
// Rubyメソッドは書かない
// ヘルパー関数は書かない

#pragma once
#include <mruby.h>
#include <stdbool.h>
#include <SDL3/SDL.h>

// モジュール登録用の初期化関数
void nx_input_init(mrb_state *mrb);

// 毎フレーム呼ぶ更新関数
void nx_input_handle_event(const SDL_Event *event);
void nx_input_update(void);