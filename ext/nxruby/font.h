#pragma once
#include <ruby.h>
#include <SDL3_ttf/SDL_ttf.h>

// Rubyの Font オブジェクトが持つデータ
typedef struct {
    TTF_Font *font;
    int size;
} NxFont;

// Fontクラスの初期化
void nx_font_init(void);
// window.c から安全にフォントデータを取り出すための内部API
TTF_Font* nx_font_get_ptr(VALUE font_obj);