#pragma once
#include <mruby.h>

// モジュール登録用の初期化関数
void nx_window_init(mrb_state *mrb);

// --- 毎フレーム処理・終了処理用の関数 ---
bool nx_window_tick(void);
void nx_window_cleanup(void);

// 各関数のプロトタイプ宣言
mrb_value nx_window_loop(mrb_state *mrb, mrb_value self);
mrb_value nx_window_close(mrb_state *mrb, mrb_value self);

mrb_value nx_window_caption(mrb_state *mrb, mrb_value self);
mrb_value nx_window_set_caption(mrb_state *mrb, mrb_value self);

mrb_value nx_window_width(mrb_state *mrb, mrb_value self);
mrb_value nx_window_set_width(mrb_state *mrb, mrb_value self);

mrb_value nx_window_height(mrb_state *mrb, mrb_value self);
mrb_value nx_window_set_height(mrb_state *mrb, mrb_value self);

mrb_value nx_window_bgcolor(mrb_state *mrb, mrb_value self);
mrb_value nx_window_set_bgcolor(mrb_state *mrb, mrb_value self);

mrb_value nx_window_fps(mrb_state *mrb, mrb_value self);
mrb_value nx_window_real_fps(mrb_state *mrb, mrb_value self);
mrb_value nx_window_set_fps(mrb_state *mrb, mrb_value self);
