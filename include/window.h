// window.c
//
// 外部のC言語から呼ばれる関数のみを書く
// Rubyメソッドは書かない
// ヘルパー関数は書かない

#pragma once
#include <mruby.h>
#include <SDL3/SDL.h>
#include <stdbool.h>

// モジュール登録用の初期化関数
void nx_window_init(mrb_state *mrb);

// 終了関数
void nx_window_cleanup(void);

// 毎フレーム呼ぶ処理関数
bool nx_window_tick(void);

// レンダラー共有用の関数
SDL_Renderer* nx_window_get_renderer(void);

// マウスの座標変換用の関数
void nx_window_convert_event(SDL_Event *event);

// マウスの座標入力用
void nx_window_set_mouse_pos(float x, float y);
