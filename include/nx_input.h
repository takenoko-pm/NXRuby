#pragma once
#include <mruby.h>

// --- モジュール登録用の初期化関数 ---
void nx_input_init(mrb_state *mrb);

// --- 毎フレーム呼ぶ更新関数 ---
void nx_input_poll(void);   // SDLから物理的な入力を取得
void nx_input_update(void); // 論理フレームを進める

// --- mruby API 用関数 ---
mrb_value nx_input_set_repeat(mrb_state *mrb, mrb_value self);
mrb_value nx_input_x(mrb_state *mrb, mrb_value self);
mrb_value nx_input_y(mrb_state *mrb, mrb_value self);
mrb_value nx_input_key_down(mrb_state *mrb, mrb_value self);
mrb_value nx_input_key_push(mrb_state *mrb, mrb_value self);
mrb_value nx_input_key_release(mrb_state *mrb, mrb_value self);

mrb_value nx_input_mouse_x(mrb_state *mrb, mrb_value self);
mrb_value nx_input_mouse_y(mrb_state *mrb, mrb_value self);
mrb_value nx_input_mouse_down(mrb_state *mrb, mrb_value self);
mrb_value nx_input_mouse_push(mrb_state *mrb, mrb_value self);
mrb_value nx_input_mouse_release(mrb_state *mrb, mrb_value self);