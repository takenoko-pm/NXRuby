#include <ruby.h>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include "window.h"
#include "image.h"

// ================================================================================
// [1] データ定義とメモリ管理
// ================================================================================

// GCから呼ばれるメモリ解放関数
static void nx_image_free(void *ptr) {
    if (!ptr) return;
    NxImage *img = (NxImage*)ptr;
    
    if (img->shared_tex) {
        img->shared_tex->ref_count--;
        if (img->shared_tex->ref_count <= 0) {
            if (img->shared_tex->texture) {
                SDL_DestroyTexture(img->shared_tex->texture);
            }
            ruby_xfree(img->shared_tex);
        }
    }
    ruby_xfree(img);
}

// CRuby用の型定義 (名前と解放関数の紐付け)
static const rb_data_type_t nx_image_data_type = {
    "NXRuby::Image",
    { NULL, nx_image_free, NULL, },
    NULL, NULL,
    RUBY_TYPED_FREE_IMMEDIATELY
};

// 外部からデータを取り出すための関数
NxImage* nx_image_get_data(VALUE image_obj) {
    if (NIL_P(image_obj)) return NULL;
    NxImage *img;
    TypedData_Get_Struct(image_obj, NxImage, &nx_image_data_type, img);
    return img;
}

// ================================================================================
// [2] RubyAPI: Imageクラスの実装
// ================================================================================

// --- Image#initialize (Image.new(w, h, color) 用) ---
static VALUE nx_image_initialize(int argc, VALUE *argv, VALUE self) {
    VALUE rw, rh, rcolor;
    rb_scan_args(argc, argv, "21", &rw, &rh, &rcolor);

    int w = NUM2INT(rw);
    int h = NUM2INT(rh);
    if (w <= 0 || h <= 0) rb_raise(rb_eArgError, "Width and height must be > 0");

    Uint8 r = 0, g = 0, b = 0, a = 0;
    if (!NIL_P(rcolor)) {
        Check_Type(rcolor, T_ARRAY);
        long len = RARRAY_LEN(rcolor);
        if (len >= 3) {
            r = (Uint8)NUM2INT(rb_ary_entry(rcolor, 0));
            g = (Uint8)NUM2INT(rb_ary_entry(rcolor, 1));
            b = (Uint8)NUM2INT(rb_ary_entry(rcolor, 2));
            a = (len >= 4) ? (Uint8)NUM2INT(rb_ary_entry(rcolor, 3)) : 255;
        }
    }

    SDL_Renderer *renderer = nx_window_get_renderer();
    if (!renderer) rb_raise(rb_eRuntimeError, "Renderer is not initialized.");

    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!texture) rb_raise(rb_eRuntimeError, "Failed to create texture: %s", SDL_GetError());
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);

    size_t pixel_count = (size_t)(w * h);
    Uint8 *pixels = (Uint8*)ruby_xmalloc(pixel_count * 4);
    for (size_t i = 0; i < pixel_count; i++) {
        pixels[i * 4 + 0] = r; pixels[i * 4 + 1] = g; pixels[i * 4 + 2] = b; pixels[i * 4 + 3] = a;
    }
    SDL_UpdateTexture(texture, NULL, pixels, w * 4);
    ruby_xfree(pixels);

    NxSharedTexture *shared = ALLOC(NxSharedTexture);
    shared->texture = texture;
    shared->ref_count = 1;

    NxImage *img = ALLOC(NxImage);
    img->shared_tex = shared;
    img->src_rect = (SDL_FRect){0, 0, (float)w, (float)h};
    img->width = (float)w;
    img->height = (float)h;

    // Cの構造体を Rubyオブジェクトに紐付ける
    DATA_PTR(self) = img;
    return self;
}

// --- Image.load ---
static VALUE nx_image_load(VALUE klass, VALUE rpath) {
    const char *path = StringValueCStr(rpath);
    SDL_Renderer *renderer = nx_window_get_renderer();
    if (!renderer) rb_raise(rb_eRuntimeError, "Renderer is not initialized.");

    SDL_Texture *texture = IMG_LoadTexture(renderer, path);
    if (!texture) rb_raise(rb_eRuntimeError, "Failed to load image '%s': %s", path, SDL_GetError());
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);

    float w, h;
    SDL_GetTextureSize(texture, &w, &h);

    NxSharedTexture *shared = ALLOC(NxSharedTexture);
    shared->texture = texture;
    shared->ref_count = 1;

    NxImage *img = ALLOC(NxImage);
    img->shared_tex = shared;
    img->src_rect = (SDL_FRect){0, 0, w, h};
    img->width = w;
    img->height = h;

    // 新しいインスタンスを生成してデータをラップする
    return TypedData_Wrap_Struct(klass, &nx_image_data_type, img);
}

// --- Image.load_tiles ---
static VALUE nx_image_load_tiles(VALUE klass, VALUE rpath, VALUE rxc, VALUE ryc) {
    const char *path = StringValueCStr(rpath);
    int xc = NUM2INT(rxc);
    int yc = NUM2INT(ryc);
    if (xc <= 0 || yc <= 0) rb_raise(rb_eArgError, "Invalid tile count");

    SDL_Renderer *renderer = nx_window_get_renderer();
    SDL_Texture *texture = IMG_LoadTexture(renderer, path);
    if (!texture) rb_raise(rb_eRuntimeError, "Failed to load image '%s': %s", path, SDL_GetError());

    float tw, th;
    SDL_GetTextureSize(texture, &tw, &th);

    NxSharedTexture *shared = ALLOC(NxSharedTexture);
    shared->texture = texture;
    shared->ref_count = xc * yc;

    float tile_w = tw / xc;
    float tile_h = th / yc;

    VALUE ary = rb_ary_new();
    for (int y = 0; y < yc; y++) {
        for (int x = 0; x < xc; x++) {
            NxImage *img = ALLOC(NxImage);
            img->shared_tex = shared;
            img->src_rect = (SDL_FRect){x * tile_w, y * tile_h, tile_w, tile_h};
            img->width = tile_w;
            img->height = tile_h;
            rb_ary_push(ary, TypedData_Wrap_Struct(klass, &nx_image_data_type, img));
        }
    }
    return ary;
}

// --- Image#slice ---
static VALUE nx_image_slice(VALUE self, VALUE rx, VALUE ry, VALUE rw, VALUE rh) {
    NxImage *src = nx_image_get_data(self);
    if (!src) return Qnil;

    NxImage *new_img = ALLOC(NxImage);
    new_img->shared_tex = src->shared_tex;
    new_img->shared_tex->ref_count++;

    new_img->src_rect.x = src->src_rect.x + (float)NUM2DBL(rx);
    new_img->src_rect.y = src->src_rect.y + (float)NUM2DBL(ry);
    new_img->src_rect.w = (float)NUM2DBL(rw);
    new_img->src_rect.h = (float)NUM2DBL(rh);
    new_img->width = new_img->src_rect.w;
    new_img->height = new_img->src_rect.h;

    return TypedData_Wrap_Struct(rb_obj_class(self), &nx_image_data_type, new_img);
}

// --- Image#width ---
static VALUE nx_image_width(VALUE self) {
    NxImage *img = nx_image_get_data(self);
    return DBL2NUM(img ? img->width : 0);
}

// --- Image#height ---
static VALUE nx_image_height(VALUE self) {
    NxImage *img = nx_image_get_data(self);
    return DBL2NUM(img ? img->height : 0);
}

// --- Image#dispose ---
static VALUE nx_image_dispose(VALUE self) {
    NxImage *img = nx_image_get_data(self);
    if (img) {
        nx_image_free(img);
        DATA_PTR(self) = NULL;  // 二重解放防止
    }
    return Qnil;
}

static VALUE nx_image_is_disposed(VALUE self) {
    return DATA_PTR(self) == NULL ? Qtrue : Qfalse;
}

static VALUE nx_image_alloc(VALUE klass) {
    return TypedData_Wrap_Struct(klass, &nx_image_data_type, NULL);
}

// ================================================================================
// [3] 初期化とメソッド登録
// ================================================================================

void nx_image_init(void) {
    VALUE cImage = rb_define_class("Image", rb_cObject);
    // Data_Wrap_Struct を使うクラスに必要な指定
    rb_define_alloc_func(cImage, nx_image_alloc);

    // クラスメソッド (Image.load)
    rb_define_singleton_method(cImage, "load", nx_image_load, 1);
    rb_define_singleton_method(cImage, "load_tiles", nx_image_load_tiles, 3);

    // インスタンスメソッド
    rb_define_method(cImage, "initialize", nx_image_initialize, -1);
    rb_define_method(cImage, "slice", nx_image_slice, 4);
    rb_define_method(cImage, "width", nx_image_width, 0);
    rb_define_method(cImage, "height", nx_image_height, 0);
    rb_define_method(cImage, "dispose", nx_image_dispose, 0);
    rb_define_method(cImage, "disposed?", nx_image_is_disposed, 0);
}