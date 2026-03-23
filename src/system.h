#pragma once
#include <mruby.h>

// モジュール登録用の初期化関数
void nx_system_init(mrb_state *mrb);

// 各関数のプロトタイプ宣言
mrb_value nx_system_log(mrb_state *mrb, mrb_value self);
