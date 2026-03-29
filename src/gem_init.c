#include <mruby.h>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include "window.h"
#include "input.h"
#include "image.h"
#include "font.h"
#include "sound.h"
#include "sprite.h"

// Gemが読み込まれた時に自動で呼ばれる
void mrb_mruby_nxruby_gem_init(mrb_state *mrb) {
    // ここにinitを追加
    nx_window_init(mrb);
    nx_input_init(mrb);
    nx_image_init(mrb);
    nx_font_init(mrb);
    nx_sound_init(mrb);
    nx_sprite_init(mrb);
}

// アプリ終了時に自動で呼ばれる
void mrb_mruby_nxruby_gem_final(mrb_state *mrb) {
    // ここにcleanupを追加
    nx_window_cleanup();
    nx_sound_cleanup();
    TTF_Quit();
}