// image.c
//
// 外部のC言語から呼ばれる関数のみを書く
// Rubyメソッドは書かない
// ヘルパー関数は書かない

#pragma once
#include <mruby.h>
#include <SDL3/SDL.h>

// --- 共有テクスチャ管理用構造体 ---
// 複数の Image オブジェクトで1つのテクスチャを使い回すための仕組み
typedef struct {
    SDL_Texture *texture;
    int ref_count; // これを使っている Image の数
} NxSharedTexture;

// Rubyの Image オブジェクトが持つデータ
typedef struct {
    NxSharedTexture *shared_tex; // 大元のテクスチャへのポインタ
    SDL_FRect src_rect;          // この画像が切り出されている範囲
    float width;                 // Ruby側から見える幅
    float height;                // Ruby側から見える高さ
} NxImage;

// Imageクラスの初期化
void nx_image_init(mrb_state *mrb);
// window.c から安全にデータを取り出すための Getter
NxImage* nx_image_get_data(mrb_state *mrb, mrb_value image_obj);
