#include <mruby.h>
#include "window.h"
#include "input.h"
#include "image.h"

// Gemが読み込まれた時に自動で呼ばれる
void mrb_mruby_nxruby_gem_init(mrb_state *mrb) {
    // ここにinitを追加
    nx_window_init(mrb);
    nx_input_init(mrb);
    nx_image_init(mrb);
}

// アプリ終了時に自動で呼ばれる
void mrb_mruby_nxruby_gem_final(mrb_state *mrb) {
    // ここにcleanupを追加
    nx_window_cleanup();
}