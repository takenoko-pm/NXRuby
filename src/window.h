#pragma once
#include <mruby.h>

// モジュール登録用の初期化関数
void nx_window_init(mrb_state *mrb);

// 各関数のプロトタイプ宣言
mrb_value nx_window_loop(mrb_state *mrb, mrb_value self);
mrb_value nx_window_color(mrb_state *mrb, mrb_value self);

// --- 毎フレーム処理・終了処理用の関数 ---
bool nx_window_tick(void);
void nx_window_cleanup(void);
