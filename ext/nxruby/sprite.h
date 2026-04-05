#pragma once
#include <ruby.h>
#include <stdbool.h>

// Spriteが持つデータ構造
typedef struct {
    float x;
    float y;
    float z;
    VALUE image; // RubyのImageオブジェクトへの参照を保持
    bool visible;    // 描画するかどうか
    bool collision_enable;
    bool vanished;

    // 拡張描画用のプロパティ
    float angle;            // 回転角度 (0〜360)
    float scale_x;          // X方向の拡大率 (1.0が等倍)
    float scale_y;          // Y方向の拡大率 (1.0が等倍)
    float center_x;         // 回転・拡大の中心X (-9999.0で自動中央)
    float center_y;         // 回転・拡大の中心Y (-9999.0で自動中央)
    bool center_x_defined;  // X中心が設定されたか
    bool center_y_defined;  // Y中心が設定されたか
    int   alpha;            // 不透明度 (0〜255)
    VALUE blend;        // 合成モード (:alpha, :add, :sub, :mod など)
} NxSprite;

// 初期化関数
void nx_sprite_init(void);