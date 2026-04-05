#pragma once
#include <ruby.h>
#include <SDL3/SDL.h>
#include <stdbool.h>

// モジュール登録用の初期化関数
void nx_window_init(void);

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

// C言語内部から直接描画キューに積むための高速API
void nx_window_draw_sprite_c(float x, float y, float z, VALUE image, float angle, float scale_x, float scale_y, float center_x, float center_y, bool cx_def, bool cy_def, int alpha, VALUE blend);
