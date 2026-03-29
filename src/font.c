#include <mruby.h>
#include <mruby/data.h>
#include <mruby/class.h>
#include <mruby/error.h>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include "font.h"

// ================================================================================
// [1] データ定義と内部状態
// ================================================================================

static void nx_font_free(mrb_state *mrb, void *ptr) {
    if (!ptr) return;
    NxFont *font = (NxFont*)ptr;
    if (font->font) {
        TTF_CloseFont(font->font);
    }
    mrb_free(mrb, font);
}

static const struct mrb_data_type nx_font_type = { "Font", nx_font_free };

TTF_Font* nx_font_get_ptr(mrb_state *mrb, mrb_value font_obj) {
    NxFont *font = (NxFont*)mrb_data_get_ptr(mrb, font_obj, &nx_font_type);
    return font ? font->font : NULL;
}

// ================================================================================
// [2] Ruby API: Fontクラスの実装
// ================================================================================

// --- Font.new(size, "assets/myfont.ttf") ---
static mrb_value nx_font_initialize(mrb_state *mrb, mrb_value self) {
    mrb_int size;
    char *path = NULL;
    mrb_get_args(mrb, "iz", &size, &path);

    if (!path) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "Font path must be specified.");
    }

    // SDL3_ttf ではサイズに float を指定します
    TTF_Font *ttf_font = TTF_OpenFont(path, (float)size);
    if (!ttf_font) {
        mrb_raisef(mrb, E_RUNTIME_ERROR, "Failed to load font '%s': %s", path, SDL_GetError());
    }

    NxFont *font = mrb_malloc(mrb, sizeof(NxFont));
    font->font = ttf_font;
    font->size = (int)size;

    DATA_PTR(self) = font;
    DATA_TYPE(self) = &nx_font_type;

    return self;
}

// --- Font#dispose ---
static mrb_value nx_font_dispose(mrb_state *mrb, mrb_value self) {
    NxFont *font = (NxFont*)mrb_data_get_ptr(mrb, self, &nx_font_type);
    if (font && font->font) {
        TTF_CloseFont(font->font);
        font->font = NULL;
    }
    return mrb_nil_value();
}

static mrb_value nx_font_is_disposed(mrb_state *mrb, mrb_value self) {
    NxFont *font = (NxFont*)mrb_data_get_ptr(mrb, self, &nx_font_type);
    return mrb_bool_value(!font || font->font == NULL);
}

static mrb_value nx_font_size(mrb_state *mrb, mrb_value self) {
    NxFont *font = (NxFont*)mrb_data_get_ptr(mrb, self, &nx_font_type);
    return mrb_int_value(mrb, font ? font->size : 0);
}

// --- Font#get_width("string") ---
static mrb_value nx_font_get_width(mrb_state *mrb, mrb_value self) {
    const char *text;
    // 引数として文字列を受け取る
    mrb_get_args(mrb, "z", &text);

    NxFont *font = (NxFont*)mrb_data_get_ptr(mrb, self, &nx_font_type);
    if (!font || !font->font) return mrb_int_value(mrb, 0);

    int w = 0, h = 0;
    // SDL3_ttf の機能で、実際に描画した場合の幅(w)と高さ(h)を計算させる
    // (第3引数の 0 は「文字列の最後まで(null終端)全部計算してね」)
    TTF_GetStringSize(font->font, text, 0, &w, &h);

    return mrb_int_value(mrb, w);
}

// ================================================================================
// [3] 初期化とメソッド登録
// ================================================================================

static const struct nx_method_table {
    const char *name;
    mrb_func_t  func;
    mrb_aspec   aspec;
} font_methods[] = {
    {"initialize", nx_font_initialize , MRB_ARGS_REQ(2)},
    {"dispose"   , nx_font_dispose    , MRB_ARGS_NONE()},
    {"disposed?" , nx_font_is_disposed, MRB_ARGS_NONE()},
    {"size"      , nx_font_size       , MRB_ARGS_NONE()},
    {"get_width" , nx_font_get_width  , MRB_ARGS_REQ(1)},
    
    // 終端マーク
    {NULL, NULL, 0} 
};

void nx_font_init(mrb_state *mrb) {
    // SDL_ttfの初期化（複数回呼んでも安全な設計になっています）
    TTF_Init();

    struct RClass *Font = mrb_define_class(mrb, "Font", mrb->object_class);
    MRB_SET_INSTANCE_TT(Font, MRB_TT_DATA);

    for (int i = 0; font_methods[i].name; i++) {
        mrb_define_method(mrb, Font, font_methods[i].name, font_methods[i].func, font_methods[i].aspec);
    }
}