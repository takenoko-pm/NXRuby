#pragma once
#include <mruby.h>
#include <SDL3_ttf/SDL_ttf.h>

// Rubyの Font オブジェクトが持つデータ
typedef struct {
    TTF_Font *font;
    int size;
} NxFont;

// Fontクラスの初期化
void nx_font_init(mrb_state *mrb);
// window.c から安全にフォントデータを取り出すための Getter
TTF_Font* nx_font_get_ptr(mrb_state *mrb, mrb_value font_obj);