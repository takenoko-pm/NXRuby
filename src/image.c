#include <mruby.h>
#include <mruby/data.h>
#include <mruby/class.h>
#include <mruby/array.h>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include "window.h"
#include "image.h"

// ================================================================================
// [1] データ定義と内部状態
// ================================================================================

// --- 内部関数の前方宣言 ---
static void nx_image_free(mrb_state *mrb, void *ptr);

// カスタムデータ型の定義（"Image" という名前と、解放関数をセット）
static const struct mrb_data_type nx_image_type = { "Image", nx_image_free };

// ================================================================================
// [2] 内部関数 (メモリ管理とGetter)
// ================================================================================

// GCから呼ばれるメモリ解放関数（参照カウント方式）
static void nx_image_free(mrb_state *mrb, void *ptr) {
    if (!ptr) return;
    NxImage *img = (NxImage*)ptr;
    
    // 共有テクスチャの参照を減らす
    if (img->shared_tex) {
        img->shared_tex->ref_count--;
        // 誰も使わなくなったら、ここで初めてGPUからテクスチャを消す
        if (img->shared_tex->ref_count <= 0) {
            if (img->shared_tex->texture) {
                SDL_DestroyTexture(img->shared_tex->texture);
            }
            mrb_free(mrb, img->shared_tex);
        }
    }
    // NxImage構造体自体をメモリ解放
    mrb_free(mrb, img);
}

// window.c からデータを取り出すための Getter
NxImage* nx_image_get_data(mrb_state *mrb, mrb_value image_obj) {
    return (NxImage*)mrb_data_get_ptr(mrb, image_obj, &nx_image_type);
}

// ================================================================================
// [3] RubyAPI: Imageクラスの実装
// ================================================================================

// --- Image.load ---
static mrb_value nx_image_load(mrb_state *mrb, mrb_value self) {
    char *path;
    mrb_get_args(mrb, "z", &path);

    SDL_Renderer *renderer = nx_window_get_renderer();
    if (!renderer) mrb_raise(mrb, E_RUNTIME_ERROR, "Renderer is not initialized.");

    SDL_Texture *texture = IMG_LoadTexture(renderer, path);
    if (!texture) mrb_raisef(mrb, E_RUNTIME_ERROR, "Failed to load image '%s': %s", path, SDL_GetError());
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);

    float w, h;
    SDL_GetTextureSize(texture, &w, &h);

    // 共有テクスチャの生成（最初は1人だけ使っている）
    NxSharedTexture *shared = mrb_malloc(mrb, sizeof(NxSharedTexture));
    shared->texture = texture;
    shared->ref_count = 1;

    // Imageデータの生成（全体を切り取り範囲とする）
    NxImage *img = mrb_malloc(mrb, sizeof(NxImage));
    img->shared_tex = shared;
    img->src_rect = (SDL_FRect){0, 0, w, h};
    img->width = w;
    img->height = h;

    struct RClass *image_class = mrb_class_get(mrb, "Image");
    return mrb_obj_value(mrb_data_object_alloc(mrb, image_class, img, &nx_image_type));
}

// --- Image.load_tiles ---
static mrb_value nx_image_load_tiles(mrb_state *mrb, mrb_value self) {
    char *path;
    mrb_int xcount, ycount;
    mrb_get_args(mrb, "zii", &path, &xcount, &ycount);

    if (xcount <= 0 || ycount <= 0) mrb_raise(mrb, E_ARGUMENT_ERROR, "Invalid tile count");

    SDL_Renderer *renderer = nx_window_get_renderer();
    SDL_Texture *texture = IMG_LoadTexture(renderer, path);
    if (!texture) mrb_raisef(mrb, E_RUNTIME_ERROR, "Failed to load image '%s': %s", path, SDL_GetError());
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);

    float tw, th;
    SDL_GetTextureSize(texture, &tw, &th);

    // 共有テクスチャを1つだけ作る
    NxSharedTexture *shared = mrb_malloc(mrb, sizeof(NxSharedTexture));
    shared->texture = texture;
    shared->ref_count = (int)(xcount * ycount); // タイルの数だけ参照される

    float tile_w = tw / xcount;
    float tile_h = th / ycount;

    // 戻り値用の配列を作る
    mrb_value ary = mrb_ary_new(mrb);
    struct RClass *image_class = mrb_class_get(mrb, "Image");

    // 分割して配列に詰めていく
    for (int y = 0; y < ycount; y++) {
        for (int x = 0; x < xcount; x++) {
            NxImage *img = mrb_malloc(mrb, sizeof(NxImage));
            img->shared_tex = shared;
            img->src_rect = (SDL_FRect){x * tile_w, y * tile_h, tile_w, tile_h};
            img->width = tile_w;
            img->height = tile_h;

            mrb_value obj = mrb_obj_value(mrb_data_object_alloc(mrb, image_class, img, &nx_image_type));
            mrb_ary_push(mrb, ary, obj);
        }
    }
    return ary;
}

// --- Image#slice ---
static mrb_value nx_image_slice(mrb_state *mrb, mrb_value self) {
    mrb_float x, y, w, h;
    mrb_get_args(mrb, "ffff", &x, &y, &w, &h);

    NxImage *src_img = nx_image_get_data(mrb, self);
    if (!src_img || !src_img->shared_tex) return mrb_nil_value();

    NxImage *new_img = mrb_malloc(mrb, sizeof(NxImage));
    new_img->shared_tex = src_img->shared_tex;
    new_img->shared_tex->ref_count++; // 参照カウントを1増やす

    // 元画像の src_rect を基準にさらに切り取る
    new_img->src_rect.x = src_img->src_rect.x + (float)x;
    new_img->src_rect.y = src_img->src_rect.y + (float)y;
    new_img->src_rect.w = (float)w;
    new_img->src_rect.h = (float)h;
    new_img->width = (float)w;
    new_img->height = (float)h;

    struct RClass *image_class = mrb_class_get(mrb, "Image");
    return mrb_obj_value(mrb_data_object_alloc(mrb, image_class, new_img, &nx_image_type));
}

// --- Image#width ---
static mrb_value nx_image_width(mrb_state *mrb, mrb_value self) {
    NxImage *img = nx_image_get_data(mrb, self);
    return mrb_int_value(mrb, img ? (mrb_int)img->width : 0);
}

// --- Image#height ---
static mrb_value nx_image_height(mrb_state *mrb, mrb_value self) {
    NxImage *img = nx_image_get_data(mrb, self);
    return mrb_int_value(mrb, img ? (mrb_int)img->height : 0);
}

// --- Image#dispose ---
static mrb_value nx_image_dispose(mrb_state *mrb, mrb_value self) {
    NxImage *img = nx_image_get_data(mrb, self);
    if (img) {
        nx_image_free(mrb, img); // 内部関数を使って安全に解放
        DATA_PTR(self) = NULL;   // 二重解放防止
    }
    return mrb_nil_value();
}

static mrb_value nx_image_is_disposed(mrb_state *mrb, mrb_value self) {
    return mrb_bool_value(DATA_PTR(self) == NULL);
}

// ================================================================================
// [4] 初期化とメソッド登録
// ================================================================================

// 登録用テーブル
static const struct nx_method_table {
    const char *name;
    mrb_func_t  func;
    mrb_aspec   aspec;
} image_methods[] = {
    {"load"            , nx_image_load             , MRB_ARGS_REQ(1)},
    {"load_tiles"      , nx_image_load_tiles       , MRB_ARGS_REQ(3)},
    {"slice"           , nx_image_slice            , MRB_ARGS_REQ(4)},
    {"width"           , nx_image_width            , MRB_ARGS_NONE()},
    {"height"          , nx_image_height           , MRB_ARGS_NONE()},
    {"dispose"         , nx_image_dispose          , MRB_ARGS_NONE()},
    {"disposed?"       , nx_image_is_disposed      , MRB_ARGS_NONE()},

    // 終端マーク
    {NULL, NULL, 0} 
};

// Imageクラスの初期化
void nx_image_init(mrb_state *mrb) {
    // class Image を定義 (Objectクラスを継承)
    struct RClass *Image = mrb_define_class(mrb, "Image", mrb->object_class);
    
    // このクラスのインスタンスがC言語の構造体(ポインタ)を持つことを宣言
    MRB_SET_INSTANCE_TT(Image, MRB_TT_DATA);

    // メソッドの登録
    for (int i = 0; image_methods[i].name; i++) {
        mrb_define_class_method(mrb, Image, image_methods[i].name, image_methods[i].func, image_methods[i].aspec);
    }
}
