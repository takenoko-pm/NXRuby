#include <mruby.h>
#include <mruby/data.h>
#include <mruby/class.h>
#include <mruby/array.h>
#include <mruby/hash.h>
#include <mruby/variable.h>
#include "sprite.h"
#include "window.h"
#include "image.h"

// ================================================================================
// [1] データ定義とメモリ管理
// ================================================================================

static void nx_sprite_free(mrb_state *mrb, void *ptr) {
    if (ptr) mrb_free(mrb, ptr);
}

static const struct mrb_data_type nx_sprite_type = { "Sprite", nx_sprite_free };

static mrb_sym sym_update;
static mrb_sym sym_draw;
static mrb_sym sym_shot;
static mrb_sym sym_hit;

// ================================================================================
// [2] 内部ヘルパー関数
// ================================================================================

// --- C言語内部での当たり判定計算 ---
static bool check_collision(mrb_state *mrb, mrb_value val1, mrb_value val2) {
    NxSprite *s1 = (NxSprite*)mrb_data_get_ptr(mrb, val1, &nx_sprite_type);
    NxSprite *s2 = (NxSprite*)mrb_data_get_ptr(mrb, val2, &nx_sprite_type);

    if (!s1 || !s2 || !s1->collision_enable || !s2->collision_enable || s1->vanished || s2->vanished) return false;
    if (mrb_nil_p(s1->image) || mrb_nil_p(s2->image)) return false;

    // 画像サイズを取得
    NxImage *img1 = nx_image_get_data(mrb, s1->image);
    NxImage *img2 = nx_image_get_data(mrb, s2->image);

    // 万が一Imageオブジェクト以外がセットされていた場合の安全対策
    if (!img1 || !img2) return false;

    float w1 = img1->width;
    float h1 = img1->height;
    float w2 = img2->width;
    float h2 = img2->height;

    // AABB (矩形) 判定
    return (s1->x < s2->x + w2) && (s1->x + w1 > s2->x) &&
           (s1->y < s2->y + h2) && (s1->y + h1 > s2->y);
}

// --- 配列の中身のメソッドを全部呼ぶ ---
static void nx_call_method_on_array(mrb_state *mrb, mrb_value obj, mrb_sym sym) {
    if (mrb_array_p(obj)) {
        mrb_int len = RARRAY_LEN(obj);
        for (mrb_int i = 0; i < len; i++) {
            mrb_value v = mrb_ary_ref(mrb, obj, i);
            if (!mrb_nil_p(v)) {
                NxSprite *s = (NxSprite*)mrb_data_get_ptr(mrb, v, &nx_sprite_type);
                // vanish済みのものや破棄済みのものはスキップ
                if (s && (s->vanished || DATA_PTR(v) == NULL)) continue;
                mrb_funcall_argv(mrb, v, sym, 0, NULL);
            }
        }
    } else if (!mrb_nil_p(obj)) {
        // 配列ではなく単体のSpriteが渡された場合
        mrb_funcall_argv(mrb, obj, sym, 0, NULL);
    }
}

// ================================================================================
// [3] Ruby API: クラスメソッド (配列一括処理など)
// ================================================================================

// --- Sprite.check(o1, o2) ---
static mrb_value nx_sprite_check(mrb_state *mrb, mrb_value self) {
    mrb_value o1, o2;
    mrb_get_args(mrb, "oo", &o1, &o2);

    // 引数が配列でなければ配列でラップする (DXRuby仕様)
    mrb_value a1 = mrb_array_p(o1) ? o1 : mrb_ary_new_from_values(mrb, 1, &o1);
    mrb_value a2 = mrb_array_p(o2) ? o2 : mrb_ary_new_from_values(mrb, 1, &o2);

    mrb_int len1 = RARRAY_LEN(a1);
    mrb_int len2 = RARRAY_LEN(a2);

    struct RClass *sprite_class = mrb_class_get(mrb, "Sprite");

    // C言語による爆速の二重ループ
    for (mrb_int i = 0; i < len1; i++) {
        mrb_value v1 = mrb_ary_ref(mrb, a1, i);
        if (mrb_nil_p(v1) || !mrb_obj_is_kind_of(mrb, v1, sprite_class) || DATA_PTR(v1) == NULL) continue;

        for (mrb_int j = 0; j < len2; j++) {
            mrb_value v2 = mrb_ary_ref(mrb, a2, j);
            if (mrb_nil_p(v2) || !mrb_obj_is_kind_of(mrb, v2, sprite_class) || DATA_PTR(v2) == NULL) continue;
            if (mrb_obj_eq(mrb, v1, v2)) continue; // 自分自身との判定はスキップ

            if (check_collision(mrb, v1, v2)) {
                if (mrb_respond_to(mrb, v1, sym_shot)) mrb_funcall_argv(mrb, v1, sym_shot, 1, &v2);
                if (mrb_respond_to(mrb, v2, sym_hit))  mrb_funcall_argv(mrb, v2, sym_hit,  1, &v1);
            }
        }
    }
    return mrb_nil_value();
}

// --- Sprite.update(ary) ---
static mrb_value nx_sprite_s_update(mrb_state *mrb, mrb_value self) {
    mrb_value ary; mrb_get_args(mrb, "o", &ary);
    nx_call_method_on_array(mrb, ary, sym_update);
    return ary;
}

// --- Sprite.draw(ary) ---
static mrb_value nx_sprite_s_draw(mrb_state *mrb, mrb_value self) {
    mrb_value ary; mrb_get_args(mrb, "o", &ary);
    nx_call_method_on_array(mrb, ary, sym_draw);
    return ary;
}

// --- Sprite.clean(ary) ---
static mrb_value nx_sprite_s_clean(mrb_state *mrb, mrb_value self) {
    mrb_value ary; mrb_get_args(mrb, "o", &ary);
    if (!mrb_array_p(ary)) return ary;

    mrb_int len = RARRAY_LEN(ary);
    mrb_int dst = 0;
    
    // 生きている要素だけを配列の前に詰める
    for (mrb_int src = 0; src < len; src++) {
        mrb_value v = mrb_ary_ref(mrb, ary, src);
        bool keep = true;
        
        if (mrb_nil_p(v)) {
            keep = false;
        } else {
            NxSprite *s = (NxSprite*)mrb_data_get_ptr(mrb, v, &nx_sprite_type);
            if (!s || s->vanished || DATA_PTR(v) == NULL) keep = false;
        }
        if (keep) mrb_ary_set(mrb, ary, dst++, v);
    }
    
    // 余った末尾の要素を削る
    while (RARRAY_LEN(ary) > dst) mrb_ary_pop(mrb, ary);
    
    return ary;
}

// ================================================================================
// [4] Ruby API: インスタンスメソッド (操作と状態)
// ================================================================================

// --- Sprite.new(x=0, y=0, image=nil) ---
static mrb_value nx_sprite_initialize(mrb_state *mrb, mrb_value self) {
    mrb_float x = 0.0, y = 0.0;
    mrb_value img = mrb_nil_value();
    mrb_get_args(mrb, "|ffo", &x, &y, &img);

    NxSprite *sprite = mrb_malloc(mrb, sizeof(NxSprite));
    sprite->x = (float)x;
    sprite->y = (float)y;
    sprite->z = 0.0f;
    sprite->image = img;
    mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "@image"), img);
    
    // フラグ群の初期化
    sprite->visible = true;
    sprite->collision_enable = true;
    sprite->vanished = false;

    // 拡張プロパティの初期化
    sprite->angle = 0.0f;
    sprite->scale_x = 1.0f;
    sprite->scale_y = 1.0f;
    sprite->center_x = 0.0f;
    sprite->center_y = 0.0f;
    sprite->center_x_defined = false;
    sprite->center_y_defined = false;
    sprite->alpha = 255;
    sprite->blend = mrb_symbol_value(mrb_intern_cstr(mrb, "alpha"));
    mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "@blend"), sprite->blend);

    DATA_PTR(self) = sprite;
    DATA_TYPE(self) = &nx_sprite_type;
    return self;
}

// --- Sprite#draw ---
static mrb_value nx_sprite_draw(mrb_state *mrb, mrb_value self) {
    NxSprite *sprite = (NxSprite*)mrb_data_get_ptr(mrb, self, &nx_sprite_type);
    if (!sprite || !sprite->visible || sprite->vanished || mrb_nil_p(sprite->image)) return mrb_nil_value();

    nx_window_draw_sprite_c(mrb, 
        sprite->x, sprite->y, sprite->z, sprite->image,
        sprite->angle, sprite->scale_x, sprite->scale_y,
        sprite->center_x, sprite->center_y, 
        sprite->center_x_defined, sprite->center_y_defined,
        sprite->alpha, sprite->blend
    );

    return mrb_nil_value();
}

// --- Sprite#===(other) ---
static mrb_value nx_sprite_eqq(mrb_state *mrb, mrb_value self) {
    mrb_value other;
    mrb_get_args(mrb, "o", &other);

    struct RClass *sprite_class = mrb_class_get(mrb, "Sprite");
    if (mrb_nil_p(other) || !mrb_obj_is_kind_of(mrb, other, sprite_class) || DATA_PTR(other) == NULL) {
        return mrb_false_value();
    }
    return mrb_bool_value(check_collision(mrb, self, other));
}

// --- Sprite#check(others) ---
static mrb_value nx_sprite_check_inst(mrb_state *mrb, mrb_value self) {
    mrb_value others;
    mrb_get_args(mrb, "o", &others);

    mrb_value a = mrb_array_p(others) ? others : mrb_ary_new_from_values(mrb, 1, &others);
    mrb_int len = RARRAY_LEN(a);
    struct RClass *sprite_class = mrb_class_get(mrb, "Sprite");
    mrb_value result = mrb_ary_new(mrb); 

    for (mrb_int i = 0; i < len; i++) {
        mrb_value v = mrb_ary_ref(mrb, a, i);
        if (mrb_nil_p(v) || !mrb_obj_is_kind_of(mrb, v, sprite_class) || DATA_PTR(v) == NULL) continue;
        if (mrb_obj_eq(mrb, self, v)) continue;

        if (check_collision(mrb, self, v)) {
            mrb_ary_push(mrb, result, v);
        }
    }
    return result;
}

// --- 消去・破棄状態の管理 ---
static mrb_value nx_sprite_dispose(mrb_state *mrb, mrb_value self) {
    NxSprite *sprite = (NxSprite*)mrb_data_get_ptr(mrb, self, &nx_sprite_type);
    if (sprite) {
        nx_sprite_free(mrb, sprite);
        DATA_PTR(self) = NULL;
    }
    return mrb_nil_value();
}

static mrb_value nx_sprite_is_disposed(mrb_state *mrb, mrb_value self) {
    return mrb_bool_value(DATA_PTR(self) == NULL);
}

static mrb_value nx_sprite_vanish(mrb_state *mrb, mrb_value self) {
    NxSprite *sprite = (NxSprite*)mrb_data_get_ptr(mrb, self, &nx_sprite_type);
    if (sprite) sprite->vanished = true;
    return self;
}

static mrb_value nx_sprite_is_vanished(mrb_state *mrb, mrb_value self) {
    NxSprite *sprite = (NxSprite*)mrb_data_get_ptr(mrb, self, &nx_sprite_type);
    return mrb_bool_value(sprite ? sprite->vanished : true);
}


// ================================================================================
// [5] Ruby API: プロパティ (Getter / Setter)
// ================================================================================

#define DEFINE_FLOAT_PROP(prop_name) \
    static mrb_value nx_sprite_get_##prop_name(mrb_state *mrb, mrb_value self) { \
        NxSprite *sprite = (NxSprite*)mrb_data_get_ptr(mrb, self, &nx_sprite_type); \
        return mrb_float_value(mrb, sprite ? sprite->prop_name : 0.0f); \
    } \
    static mrb_value nx_sprite_set_##prop_name(mrb_state *mrb, mrb_value self) { \
        mrb_float val; mrb_get_args(mrb, "f", &val); \
        NxSprite *sprite = (NxSprite*)mrb_data_get_ptr(mrb, self, &nx_sprite_type); \
        if (sprite) sprite->prop_name = (float)val; \
        return mrb_float_value(mrb, val); \
    }

static mrb_value nx_sprite_get_center_x(mrb_state *mrb, mrb_value self) {
    NxSprite *sprite = (NxSprite*)mrb_data_get_ptr(mrb, self, &nx_sprite_type);
    return mrb_float_value(mrb, sprite ? sprite->center_x : 0.0f);
}

static mrb_value nx_sprite_set_center_x(mrb_state *mrb, mrb_value self) {
    mrb_float val; mrb_get_args(mrb, "f", &val);
    NxSprite *sprite = (NxSprite*)mrb_data_get_ptr(mrb, self, &nx_sprite_type);
    if (sprite) {
        sprite->center_x = (float)val;
        sprite->center_x_defined = true;
    }
    return mrb_float_value(mrb, val);
}

static mrb_value nx_sprite_get_center_y(mrb_state *mrb, mrb_value self) {
    NxSprite *sprite = (NxSprite*)mrb_data_get_ptr(mrb, self, &nx_sprite_type);
    return mrb_float_value(mrb, sprite ? sprite->center_y : 0.0f);
}

static mrb_value nx_sprite_set_center_y(mrb_state *mrb, mrb_value self) {
    mrb_float val; mrb_get_args(mrb, "f", &val);
    NxSprite *sprite = (NxSprite*)mrb_data_get_ptr(mrb, self, &nx_sprite_type);
    if (sprite) {
        sprite->center_y = (float)val;
        sprite->center_y_defined = true;
    }
    return mrb_float_value(mrb, val);
}

#define DEFINE_BOOL_PROP(prop_name) \
    static mrb_value nx_sprite_get_##prop_name(mrb_state *mrb, mrb_value self) { \
        NxSprite *sprite = (NxSprite*)mrb_data_get_ptr(mrb, self, &nx_sprite_type); \
        return mrb_bool_value(sprite ? sprite->prop_name : false); \
    } \
    static mrb_value nx_sprite_set_##prop_name(mrb_state *mrb, mrb_value self) { \
        mrb_bool val; mrb_get_args(mrb, "b", &val); \
        NxSprite *sprite = (NxSprite*)mrb_data_get_ptr(mrb, self, &nx_sprite_type); \
        if (sprite) sprite->prop_name = val; \
        return mrb_bool_value(val); \
    }

#define DEFINE_INT_PROP(prop_name) \
    static mrb_value nx_sprite_get_##prop_name(mrb_state *mrb, mrb_value self) { \
        NxSprite *sprite = (NxSprite*)mrb_data_get_ptr(mrb, self, &nx_sprite_type); \
        return mrb_int_value(mrb, sprite ? sprite->prop_name : 0); \
    } \
    static mrb_value nx_sprite_set_##prop_name(mrb_state *mrb, mrb_value self) { \
        mrb_int val; mrb_get_args(mrb, "i", &val); \
        NxSprite *sprite = (NxSprite*)mrb_data_get_ptr(mrb, self, &nx_sprite_type); \
        if (sprite) sprite->prop_name = (int)val; \
        return mrb_int_value(mrb, val); \
    }

// --- マクロによる実体化 ---
DEFINE_FLOAT_PROP(x)
DEFINE_FLOAT_PROP(y)
DEFINE_FLOAT_PROP(z)
DEFINE_BOOL_PROP(collision_enable)
DEFINE_FLOAT_PROP(angle)
DEFINE_FLOAT_PROP(scale_x)
DEFINE_FLOAT_PROP(scale_y)
DEFINE_INT_PROP(alpha)

// --- 特殊なプロパティ (image, blend) ---
static mrb_value nx_sprite_get_image(mrb_state *mrb, mrb_value self) {
    NxSprite *sprite = (NxSprite*)mrb_data_get_ptr(mrb, self, &nx_sprite_type);
    return sprite ? sprite->image : mrb_nil_value();
}
static mrb_value nx_sprite_set_image(mrb_state *mrb, mrb_value self) {
    mrb_value img; mrb_get_args(mrb, "o", &img);
    NxSprite *sprite = (NxSprite*)mrb_data_get_ptr(mrb, self, &nx_sprite_type);
    if (sprite) {
        sprite->image = img;
        mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "@image"), img);
    }
    return img;
}

static mrb_value nx_sprite_get_blend(mrb_state *mrb, mrb_value self) {
    NxSprite *sprite = (NxSprite*)mrb_data_get_ptr(mrb, self, &nx_sprite_type);
    return sprite ? sprite->blend : mrb_nil_value();
}
static mrb_value nx_sprite_set_blend(mrb_state *mrb, mrb_value self) {
    mrb_value val; mrb_get_args(mrb, "o", &val);
    NxSprite *sprite = (NxSprite*)mrb_data_get_ptr(mrb, self, &nx_sprite_type);
    if (sprite) {
        sprite->blend = val;
        mrb_iv_set(mrb, self, mrb_intern_lit(mrb, "@blend"), val);
    }
    return val;
}

// ================================================================================
// [6] 初期化とメソッド登録
// ================================================================================

struct nx_method_table {
    const char *name;
    mrb_func_t  func;
    mrb_aspec   aspec;
};

static const struct nx_method_table sprite_class_methods[] = {
    {"check",  nx_sprite_check,    MRB_ARGS_REQ(2)},
    {"update", nx_sprite_s_update, MRB_ARGS_REQ(1)},
    {"draw",   nx_sprite_s_draw,   MRB_ARGS_REQ(1)},
    {"clean",  nx_sprite_s_clean,  MRB_ARGS_REQ(1)},
    {NULL, NULL, 0}
};

static const struct nx_method_table sprite_instance_methods[] = {
    // --- ライフサイクルと基本操作 ---
    {"initialize",        nx_sprite_initialize,           MRB_ARGS_OPT(3)},
    {"draw",              nx_sprite_draw,                 MRB_ARGS_NONE()},
    
    // --- 衝突判定 ---
    {"===",               nx_sprite_eqq,                  MRB_ARGS_REQ(1)},
    {"check",             nx_sprite_check_inst,           MRB_ARGS_REQ(1)},

    // --- 基本プロパティ (座標) ---
    {"x",                 nx_sprite_get_x,                MRB_ARGS_NONE()},
    {"x=",                nx_sprite_set_x,                MRB_ARGS_REQ(1)},
    {"y",                 nx_sprite_get_y,                MRB_ARGS_NONE()},
    {"y=",                nx_sprite_set_y,                MRB_ARGS_REQ(1)},
    {"z",                 nx_sprite_get_z,                MRB_ARGS_NONE()},
    {"z=",                nx_sprite_set_z,                MRB_ARGS_REQ(1)},
    
    // --- 拡張描画プロパティ (エフェクト) ---
    {"angle",             nx_sprite_get_angle,            MRB_ARGS_NONE()},
    {"angle=",            nx_sprite_set_angle,            MRB_ARGS_REQ(1)},
    {"scale_x",           nx_sprite_get_scale_x,          MRB_ARGS_NONE()},
    {"scale_x=",          nx_sprite_set_scale_x,          MRB_ARGS_REQ(1)},
    {"scale_y",           nx_sprite_get_scale_y,          MRB_ARGS_NONE()},
    {"scale_y=",          nx_sprite_set_scale_y,          MRB_ARGS_REQ(1)},
    {"center_x",          nx_sprite_get_center_x,         MRB_ARGS_NONE()},
    {"center_x=",         nx_sprite_set_center_x,         MRB_ARGS_REQ(1)},
    {"center_y",          nx_sprite_get_center_y,         MRB_ARGS_NONE()},
    {"center_y=",         nx_sprite_set_center_y,         MRB_ARGS_REQ(1)},
    {"alpha",             nx_sprite_get_alpha,            MRB_ARGS_NONE()},
    {"alpha=",            nx_sprite_set_alpha,            MRB_ARGS_REQ(1)},
    {"blend",             nx_sprite_get_blend,            MRB_ARGS_NONE()},
    {"blend=",            nx_sprite_set_blend,            MRB_ARGS_REQ(1)},

    // --- リソース・設定プロパティ ---
    {"image",             nx_sprite_get_image,            MRB_ARGS_NONE()},
    {"image=",            nx_sprite_set_image,            MRB_ARGS_REQ(1)},
    {"collision_enable",  nx_sprite_get_collision_enable, MRB_ARGS_NONE()},
    {"collision_enable=", nx_sprite_set_collision_enable, MRB_ARGS_REQ(1)},

    // --- メモリ管理と消去 ---
    {"vanish",            nx_sprite_vanish,               MRB_ARGS_NONE()},
    {"vanished?",         nx_sprite_is_vanished,          MRB_ARGS_NONE()},
    {"dispose",           nx_sprite_dispose,              MRB_ARGS_NONE()},
    {"disposed?",         nx_sprite_is_disposed,          MRB_ARGS_NONE()},
    
    {NULL, NULL, 0}
};

void nx_sprite_init(mrb_state *mrb) {
    sym_update = mrb_intern_cstr(mrb, "update");
    sym_draw   = mrb_intern_cstr(mrb, "draw");
    sym_shot   = mrb_intern_cstr(mrb, "shot");
    sym_hit    = mrb_intern_cstr(mrb, "hit");

    struct RClass *Sprite = mrb_define_class(mrb, "Sprite", mrb->object_class);
    MRB_SET_INSTANCE_TT(Sprite, MRB_TT_DATA);

    // クラスメソッドの登録
    for (int i = 0; sprite_class_methods[i].name; i++) {
        mrb_define_class_method(mrb, Sprite, sprite_class_methods[i].name, sprite_class_methods[i].func, sprite_class_methods[i].aspec);
    }

    // インスタンスメソッドの登録
    for (int i = 0; sprite_instance_methods[i].name; i++) {
        mrb_define_method(mrb, Sprite, sprite_instance_methods[i].name, sprite_instance_methods[i].func, sprite_instance_methods[i].aspec);
    }
}