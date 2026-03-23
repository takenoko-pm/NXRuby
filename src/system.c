#include <mruby.h>
#include <mruby/compile.h>
#include <SDL3/SDL.h>
#include "system.h"

// 汎用APIを実装するファイル

// 登録処理
// MRB_ARGS_REQ()内の数字は引数の数
void nx_system_init(mrb_state *mrb) {
    struct RClass *System = mrb_define_module(mrb, "System");
    
    mrb_define_module_function(mrb, System, "log", nx_system_log, MRB_ARGS_REQ(1));
}

mrb_value nx_system_log(mrb_state *mrb, mrb_value self) {
    char *msg;
    mrb_get_args(mrb, "z", &msg);           // Rubyから文字列を取得
    SDL_Log("NXRuby: %s", msg);             // SDLのログ機能を使用
    return self;
}
