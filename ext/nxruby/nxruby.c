#include <ruby.h>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <SDL3_mixer/SDL_mixer.h>

#include "window.h"
#include "input.h"
#include "image.h"
#include "font.h"
#include "sound.h"
#include "sprite.h"

// ================================================================================
// 終了処理
// ================================================================================
static void nxruby_cleanup(VALUE data) {
    nx_window_cleanup();
    nx_sound_cleanup();
    TTF_Quit();
    SDL_Quit();
}

// ================================================================================
// 初期化処理
// ================================================================================
void Init_nxruby(void) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        // 例外処理
        rb_raise(rb_eRuntimeError, "SDL_Init failed: %s", SDL_GetError());
    }

    nx_window_init();
    nx_input_init();
    nx_image_init();
    nx_font_init();
    nx_sound_init();
    nx_sprite_init();

    rb_set_end_proc(nxruby_cleanup, Qnil);
}