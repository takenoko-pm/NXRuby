#pragma once
#include <mruby.h>

// モジュール登録用の初期化関数
void nx_window_init(mrb_state *mrb);

// --- 毎フレーム処理・終了処理用の関数 ---
bool nx_window_tick(void);
void nx_window_cleanup(void);

// mruby 用関数のプロトタイプ宣言
mrb_value nx_window_loop(mrb_state *mrb, mrb_value self);
mrb_value nx_window_close(mrb_state *mrb, mrb_value self);

mrb_value nx_window_logic_fps(mrb_state *mrb, mrb_value self);
mrb_value nx_window_render_fps(mrb_state *mrb, mrb_value self);
mrb_value nx_window_fps(mrb_state *mrb, mrb_value self);
mrb_value nx_window_set_fps(mrb_state *mrb, mrb_value self);

mrb_value nx_window_caption(mrb_state *mrb, mrb_value self);
mrb_value nx_window_set_caption(mrb_state *mrb, mrb_value self);

mrb_value nx_window_width(mrb_state *mrb, mrb_value self);
mrb_value nx_window_set_width(mrb_state *mrb, mrb_value self);

mrb_value nx_window_height(mrb_state *mrb, mrb_value self);
mrb_value nx_window_set_height(mrb_state *mrb, mrb_value self);

mrb_value nx_window_x(mrb_state *mrb, mrb_value self);
mrb_value nx_window_set_x(mrb_state *mrb, mrb_value self);
mrb_value nx_window_y(mrb_state *mrb, mrb_value self);
mrb_value nx_window_set_y(mrb_state *mrb, mrb_value self);

mrb_value nx_window_full_screen(mrb_state *mrb, mrb_value self);
mrb_value nx_window_set_full_screen(mrb_state *mrb, mrb_value self);
mrb_value nx_window_windowed(mrb_state *mrb, mrb_value self);
mrb_value nx_window_set_windowed(mrb_state *mrb, mrb_value self);

mrb_value nx_window_running_time(mrb_state *mrb, mrb_value self);

mrb_value nx_window_scale(mrb_state *mrb, mrb_value self);
mrb_value nx_window_set_scale(mrb_state *mrb, mrb_value self);
mrb_value nx_window_active(mrb_state *mrb, mrb_value self);
mrb_value nx_window_ox(mrb_state *mrb, mrb_value self);
mrb_value nx_window_set_ox(mrb_state *mrb, mrb_value self);
mrb_value nx_window_oy(mrb_state *mrb, mrb_value self);
mrb_value nx_window_set_oy(mrb_state *mrb, mrb_value self);

mrb_value nx_window_bgcolor(mrb_state *mrb, mrb_value self);
mrb_value nx_window_set_bgcolor(mrb_state *mrb, mrb_value self);

mrb_value nx_window_draw_pixel(mrb_state *mrb, mrb_value self);
mrb_value nx_window_draw_line(mrb_state *mrb, mrb_value self);
mrb_value nx_window_draw_rect(mrb_state *mrb, mrb_value self);
mrb_value nx_window_draw_rect_fill(mrb_state *mrb, mrb_value self);
mrb_value nx_window_draw_circle(mrb_state *mrb, mrb_value self);
mrb_value nx_window_draw_circle_fill(mrb_state *mrb, mrb_value self);
