#include <ruby.h>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include "font.h"

// ================================================================================
// [1] データ定義とメモリ管理
// ================================================================================

// GCから呼ばれるメモリ解放関数
static void nx_font_free(void *ptr) {
    if (!ptr) return;
    NxFont *font = (NxFont*)ptr;
    if (font->font) {
        TTF_CloseFont(font->font);
    }
    ruby_xfree(font);
}

// CRuby用の型定義
static const rb_data_type_t nx_font_data_type = {
    "NXRuby::Font",
    { NULL, nx_font_free, NULL, }, // マーク関数, 解放関数, サイズ関数
    NULL, NULL,
    RUBY_TYPED_FREE_IMMEDIATELY
};

// 外部からフォントのポインタを取り出すための関数
TTF_Font* nx_font_get_ptr(VALUE font_obj) {
    if (NIL_P(font_obj)) return NULL;
    NxFont *font;
    TypedData_Get_Struct(font_obj, NxFont, &nx_font_data_type, font);
    return font ? font->font : NULL;
}

// ================================================================================
// [2] Ruby API: Fontクラスの実装
// ================================================================================

// --- Font.new(size, path) ---
static VALUE nx_font_initialize(VALUE self, VALUE rsize, VALUE rpath) {
    const char *path = StringValueCStr(rpath);
    int size = NUM2INT(rsize);

    if (size <= 0) rb_raise(rb_eArgError, "Font size must be > 0");

    // SDL3_ttf ではサイズに float を指定
    TTF_Font* ttf_font = TTF_OpenFont(path, (float)size);
    if (!ttf_font) {
        rb_raise(rb_eRuntimeError, "Failed to load font '%s': %s", path, SDL_GetError());
    }

    NxFont *font = ALLOC(NxFont);
    font->font = ttf_font;
    font->size = size;

    // Cの構造体を Rubyオブジェクトに紐付ける
    DATA_PTR(self) = font;

    return self;
}

// --- Font#dispose ---
static VALUE nx_font_dispose(VALUE self) {
    NxFont *font;
    TypedData_Get_Struct(self, NxFont, &nx_font_data_type, font);
    if (font && font->font) {
        TTF_CloseFont(font->font);
        font->font = NULL;
    }
    return Qnil;
}

static VALUE nx_font_is_disposed(VALUE self) {
    NxFont *font;
    TypedData_Get_Struct(self, NxFont, &nx_font_data_type, font);
    return (font && font->font) ? Qfalse : Qtrue;
}

static VALUE nx_font_size(VALUE self) {
    NxFont *font;
    TypedData_Get_Struct(self, NxFont, &nx_font_data_type, font);
    return INT2NUM(font ? font->size : 0);
}

// --- Font#get_width(text) ---
static VALUE nx_font_get_width(VALUE self, VALUE rtext) {
    const char *text = StringValueCStr(rtext);
    
    NxFont *font;
    TypedData_Get_Struct(self, NxFont, &nx_font_data_type, font);
    if (!font || !font->font) return INT2NUM(0);

    int w = 0, h = 0;
    // 文字列描画時のサイズを計算 (null終端まで)
    TTF_GetStringSize(font->font, text, 0, &w, &h);

    return INT2NUM(w);
}

static VALUE nx_font_alloc(VALUE klass) {
    return TypedData_Wrap_Struct(klass, &nx_font_data_type, NULL);
}

// ================================================================================
// [3] 初期化とメソッド登録
// ================================================================================

void nx_font_init(void) {
    // SDL_ttfの初期化
    TTF_Init();

    VALUE cFont = rb_define_class("Font", rb_cObject);
    rb_define_alloc_func(cFont, nx_font_alloc);

    rb_define_method(cFont, "initialize", nx_font_initialize, 2);
    rb_define_method(cFont, "dispose",    nx_font_dispose, 0);
    rb_define_method(cFont, "disposed?",  nx_font_is_disposed, 0);
    rb_define_method(cFont, "size",       nx_font_size, 0);
    rb_define_method(cFont, "get_width",  nx_font_get_width, 1);
}