#include <ruby.h>
#include <stdbool.h>
#include "sprite.h"
#include "window.h"
#include "image.h"

// ================================================================================
// [1] データ定義とメモリ管理
// ================================================================================

static void nx_sprite_free(void *ptr) {
    if (ptr) ruby_xfree(ptr);
}

static const rb_data_type_t nx_sprite_data_type = {
    "NXRuby::Sprite",
    { NULL, nx_sprite_free, NULL, },
    NULL, NULL,
    RUBY_TYPED_FREE_IMMEDIATELY
};

static ID id_update;
static ID id_draw;
static ID id_shot;
static ID id_hit;

// ================================================================================
// [2] 内部ヘルパー関数
// ================================================================================

// --- C言語内部での当たり判定計算 ---
static bool check_collision(VALUE val1, VALUE val2) {
    NxSprite *s1, *s2;
    TypedData_Get_Struct(val1, NxSprite, &nx_sprite_data_type, s1);
    TypedData_Get_Struct(val2, NxSprite, &nx_sprite_data_type, s2);

    if (!s1 || !s2 || !s1->collision_enable || !s2->collision_enable || s1->vanished || s2->vanished) return false;
    if (NIL_P(s1->image) || NIL_P(s2->image)) return false;

    NxImage *img1 = nx_image_get_data(s1->image);
    NxImage *img2 = nx_image_get_data(s2->image);

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
static void nx_call_method_on_array(VALUE obj, ID sym) {
    if (RB_TYPE_P(obj, T_ARRAY)) {
        long len = RARRAY_LEN(obj);
        for (long i = 0; i < len; i++) {
            VALUE v = rb_ary_entry(obj, i);
            if (!NIL_P(v)) {
                // 安全な型チェック: Spriteオブジェクトであればvanish状態を確認
                if (rb_typeddata_is_kind_of(v, &nx_sprite_data_type)) {
                    NxSprite *s = (NxSprite*)DATA_PTR(v);
                    if (!s || s->vanished) continue;
                }
                // Rubyのメソッドを呼ぶ
                rb_funcall(v, sym, 0);
            }
        }
    } else if (!NIL_P(obj)) {
        rb_funcall(obj, sym, 0);
    }
}

// ================================================================================
// [3] Ruby API: クラスメソッド (配列一括処理など)
// ================================================================================

// --- Sprite.check(o1, o2) ---
static VALUE nx_sprite_check(VALUE self, VALUE o1, VALUE o2) {
    VALUE a1 = RB_TYPE_P(o1, T_ARRAY) ? o1 : rb_ary_new_from_args(1, o1);
    VALUE a2 = RB_TYPE_P(o2, T_ARRAY) ? o2 : rb_ary_new_from_args(1, o2);

    long len1 = RARRAY_LEN(a1);
    long len2 = RARRAY_LEN(a2);

    VALUE cSprite = rb_const_get(rb_cObject, rb_intern("Sprite"));

    for (long i = 0; i < len1; i++) {
        VALUE v1 = rb_ary_entry(a1, i);
        if (NIL_P(v1) || !rb_obj_is_kind_of(v1, cSprite) || DATA_PTR(v1) == NULL) continue;

        for (long j = 0; j < len2; j++) {
            VALUE v2 = rb_ary_entry(a2, j);
            if (NIL_P(v2) || !rb_obj_is_kind_of(v2, cSprite) || DATA_PTR(v2) == NULL) continue;
            if (v1 == v2) continue; // 自分自身との判定はスキップ

            if (check_collision(v1, v2)) {
                if (rb_respond_to(v1, id_shot)) rb_funcall(v1, id_shot, 1, v2);
                if (rb_respond_to(v2, id_hit))  rb_funcall(v2, id_hit,  1, v1);
            }
        }
    }
    return Qnil;
}

// --- Sprite.update(ary) ---
static VALUE nx_sprite_s_update(VALUE self, VALUE ary) {
    nx_call_method_on_array(ary, id_update);
    return ary;
}

// --- Sprite.draw(ary) ---
static VALUE nx_sprite_s_draw(VALUE self, VALUE ary) {
    nx_call_method_on_array(ary, id_draw);
    return ary;
}

// --- Sprite.clean(ary) ---
static VALUE nx_sprite_s_clean(VALUE self, VALUE ary) {
    if (!RB_TYPE_P(ary, T_ARRAY)) return ary;

    long len = RARRAY_LEN(ary);
    long dst = 0;
    
    // 生きている要素だけを配列の前に詰める
    for (long src = 0; src < len; src++) {
        VALUE v = rb_ary_entry(ary, src);
        bool keep = true;
        
        if (NIL_P(v)) {
            keep = false;
        } else if (rb_typeddata_is_kind_of(v, &nx_sprite_data_type)) {
            NxSprite *s = (NxSprite*)DATA_PTR(v);
            if (!s || s->vanished) keep = false;
        }
        
        if (keep) rb_ary_store(ary, dst++, v);
    }
    
    rb_ary_resize(ary, dst);
    
    return ary;
}

// ================================================================================
// [4] Ruby API: インスタンスメソッド (操作と状態)
// ================================================================================

// --- Sprite#initialize(x=0, y=0, image=nil) ---
static VALUE nx_sprite_initialize(int argc, VALUE *argv, VALUE self) {
    VALUE rx, ry, rimg;
    rb_scan_args(argc, argv, "03", &rx, &ry, &rimg); // 省略可能引数3つ

    NxSprite *sprite = ALLOC(NxSprite);
    sprite->x = NIL_P(rx) ? 0.0f : (float)NUM2DBL(rx);
    sprite->y = NIL_P(ry) ? 0.0f : (float)NUM2DBL(ry);
    sprite->z = 0.0f;
    sprite->image = NIL_P(rimg) ? Qnil : rimg;
    
    // GCに回収されないようにインスタンス変数としてRuby側に持たせる
    rb_ivar_set(self, rb_intern("@image"), sprite->image);
    
    sprite->visible = true;
    sprite->collision_enable = true;
    sprite->vanished = false;

    sprite->angle = 0.0f;
    sprite->scale_x = 1.0f;
    sprite->scale_y = 1.0f;
    sprite->center_x = 0.0f;
    sprite->center_y = 0.0f;
    sprite->center_x_defined = false;
    sprite->center_y_defined = false;
    sprite->alpha = 255;
    sprite->blend = ID2SYM(rb_intern("alpha"));
    rb_ivar_set(self, rb_intern("@blend"), sprite->blend);

    DATA_PTR(self) = sprite;
    return self;
}

// --- Sprite#draw ---
static VALUE nx_sprite_draw(VALUE self) {
    NxSprite *sprite;
    TypedData_Get_Struct(self, NxSprite, &nx_sprite_data_type, sprite);

    if (!sprite || !sprite->visible || sprite->vanished || NIL_P(sprite->image)) return Qnil;

    nx_window_draw_sprite_c(
        sprite->x, sprite->y, sprite->z, sprite->image,
        sprite->angle, sprite->scale_x, sprite->scale_y,
        sprite->center_x, sprite->center_y, 
        sprite->center_x_defined, sprite->center_y_defined,
        sprite->alpha, sprite->blend
    );

    return Qnil;
}

// --- Sprite#===(other) ---
static VALUE nx_sprite_eqq(VALUE self, VALUE other) {
    VALUE cSprite = rb_const_get(rb_cObject, rb_intern("Sprite"));
    if (NIL_P(other) || !rb_obj_is_kind_of(other, cSprite) || DATA_PTR(other) == NULL) {
        return Qfalse;
    }
    return check_collision(self, other) ? Qtrue : Qfalse;
}

// --- Sprite#check(others) ---
static VALUE nx_sprite_check_inst(VALUE self, VALUE others) {
    VALUE a = RB_TYPE_P(others, T_ARRAY) ? others : rb_ary_new_from_args(1, others);
    long len = RARRAY_LEN(a);
    VALUE cSprite = rb_const_get(rb_cObject, rb_intern("Sprite"));
    VALUE result = rb_ary_new(); 

    for (long i = 0; i < len; i++) {
        VALUE v = rb_ary_entry(a, i);
        if (NIL_P(v) || !rb_obj_is_kind_of(v, cSprite) || DATA_PTR(v) == NULL) continue;
        if (self == v) continue;

        if (check_collision(self, v)) {
            rb_ary_push(result, v);
        }
    }
    return result;
}

// --- 消去・破棄 ---
static VALUE nx_sprite_dispose(VALUE self) {
    NxSprite *sprite;
    TypedData_Get_Struct(self, NxSprite, &nx_sprite_data_type, sprite);
    if (sprite) {
        nx_sprite_free(sprite);
        DATA_PTR(self) = NULL;
    }
    return Qnil;
}

static VALUE nx_sprite_is_disposed(VALUE self) {
    return DATA_PTR(self) == NULL ? Qtrue : Qfalse;
}

static VALUE nx_sprite_vanish(VALUE self) {
    NxSprite *sprite;
    TypedData_Get_Struct(self, NxSprite, &nx_sprite_data_type, sprite);
    if (sprite) sprite->vanished = true;
    return self;
}

static VALUE nx_sprite_is_vanished(VALUE self) {
    NxSprite *sprite;
    TypedData_Get_Struct(self, NxSprite, &nx_sprite_data_type, sprite);
    return sprite ? (sprite->vanished ? Qtrue : Qfalse) : Qtrue;
}

// ================================================================================
// [5] Ruby API: プロパティ (Getter / Setter)
// ================================================================================

#define DEFINE_FLOAT_PROP(prop_name) \
    static VALUE nx_sprite_get_##prop_name(VALUE self) { \
        NxSprite *sprite; TypedData_Get_Struct(self, NxSprite, &nx_sprite_data_type, sprite); \
        return DBL2NUM(sprite ? sprite->prop_name : 0.0f); \
    } \
    static VALUE nx_sprite_set_##prop_name(VALUE self, VALUE val) { \
        NxSprite *sprite; TypedData_Get_Struct(self, NxSprite, &nx_sprite_data_type, sprite); \
        if (sprite) sprite->prop_name = (float)NUM2DBL(val); \
        return val; \
    }

DEFINE_FLOAT_PROP(x)
DEFINE_FLOAT_PROP(y)
DEFINE_FLOAT_PROP(z)
DEFINE_FLOAT_PROP(angle)
DEFINE_FLOAT_PROP(scale_x)
DEFINE_FLOAT_PROP(scale_y)

static VALUE nx_sprite_get_center_x(VALUE self) {
    NxSprite *sprite; TypedData_Get_Struct(self, NxSprite, &nx_sprite_data_type, sprite);
    return DBL2NUM(sprite ? sprite->center_x : 0.0f);
}
static VALUE nx_sprite_set_center_x(VALUE self, VALUE val) {
    NxSprite *sprite; TypedData_Get_Struct(self, NxSprite, &nx_sprite_data_type, sprite);
    if (sprite) { sprite->center_x = (float)NUM2DBL(val); sprite->center_x_defined = true; }
    return val;
}

static VALUE nx_sprite_get_center_y(VALUE self) {
    NxSprite *sprite; TypedData_Get_Struct(self, NxSprite, &nx_sprite_data_type, sprite);
    return DBL2NUM(sprite ? sprite->center_y : 0.0f);
}
static VALUE nx_sprite_set_center_y(VALUE self, VALUE val) {
    NxSprite *sprite; TypedData_Get_Struct(self, NxSprite, &nx_sprite_data_type, sprite);
    if (sprite) { sprite->center_y = (float)NUM2DBL(val); sprite->center_y_defined = true; }
    return val;
}

#define DEFINE_BOOL_PROP(prop_name) \
    static VALUE nx_sprite_get_##prop_name(VALUE self) { \
        NxSprite *sprite; TypedData_Get_Struct(self, NxSprite, &nx_sprite_data_type, sprite); \
        return (sprite && sprite->prop_name) ? Qtrue : Qfalse; \
    } \
    static VALUE nx_sprite_set_##prop_name(VALUE self, VALUE val) { \
        NxSprite *sprite; TypedData_Get_Struct(self, NxSprite, &nx_sprite_data_type, sprite); \
        if (sprite) sprite->prop_name = RTEST(val); \
        return val; \
    }

DEFINE_BOOL_PROP(collision_enable)

#define DEFINE_INT_PROP(prop_name) \
    static VALUE nx_sprite_get_##prop_name(VALUE self) { \
        NxSprite *sprite; TypedData_Get_Struct(self, NxSprite, &nx_sprite_data_type, sprite); \
        return INT2NUM(sprite ? sprite->prop_name : 0); \
    } \
    static VALUE nx_sprite_set_##prop_name(VALUE self, VALUE val) { \
        NxSprite *sprite; TypedData_Get_Struct(self, NxSprite, &nx_sprite_data_type, sprite); \
        if (sprite) sprite->prop_name = NUM2INT(val); \
        return val; \
    }

DEFINE_INT_PROP(alpha)

// --- 特殊プロパティ (image, blend) ---
static VALUE nx_sprite_get_image(VALUE self) {
    NxSprite *sprite; TypedData_Get_Struct(self, NxSprite, &nx_sprite_data_type, sprite);
    return sprite ? sprite->image : Qnil;
}
static VALUE nx_sprite_set_image(VALUE self, VALUE img) {
    NxSprite *sprite; TypedData_Get_Struct(self, NxSprite, &nx_sprite_data_type, sprite);
    if (sprite) {
        sprite->image = img;
        rb_ivar_set(self, rb_intern("@image"), img); // Ruby側でも参照を保持（GC対策）
    }
    return img;
}

static VALUE nx_sprite_get_blend(VALUE self) {
    NxSprite *sprite; TypedData_Get_Struct(self, NxSprite, &nx_sprite_data_type, sprite);
    return sprite ? sprite->blend : Qnil;
}
static VALUE nx_sprite_set_blend(VALUE self, VALUE val) {
    NxSprite *sprite; TypedData_Get_Struct(self, NxSprite, &nx_sprite_data_type, sprite);
    if (sprite) {
        sprite->blend = val;
        rb_ivar_set(self, rb_intern("@blend"), val);
    }
    return val;
}

static VALUE nx_sprite_alloc(VALUE klass) {
    return TypedData_Wrap_Struct(klass, &nx_sprite_data_type, NULL);
}

// ================================================================================
// [6] 初期化とメソッド登録
// ================================================================================

void nx_sprite_init(void) {
    id_update = rb_intern("update");
    id_draw   = rb_intern("draw");
    id_shot   = rb_intern("shot");
    id_hit    = rb_intern("hit");

    VALUE cSprite = rb_define_class("Sprite", rb_cObject);
    rb_define_alloc_func(cSprite, nx_sprite_alloc);

    // クラスメソッド
    rb_define_singleton_method(cSprite, "check",  nx_sprite_check, 2);
    rb_define_singleton_method(cSprite, "update", nx_sprite_s_update, 1);
    rb_define_singleton_method(cSprite, "draw",   nx_sprite_s_draw, 1);
    rb_define_singleton_method(cSprite, "clean",  nx_sprite_s_clean, 1);

    // インスタンスメソッド群
    rb_define_method(cSprite, "initialize", nx_sprite_initialize, -1);
    rb_define_method(cSprite, "draw",       nx_sprite_draw, 0);
    rb_define_method(cSprite, "===",        nx_sprite_eqq, 1);
    rb_define_method(cSprite, "check",      nx_sprite_check_inst, 1);

    // プロパティ
    rb_define_method(cSprite, "x", nx_sprite_get_x, 0);
    rb_define_method(cSprite, "x=", nx_sprite_set_x, 1);
    rb_define_method(cSprite, "y", nx_sprite_get_y, 0);
    rb_define_method(cSprite, "y=", nx_sprite_set_y, 1);
    rb_define_method(cSprite, "z", nx_sprite_get_z, 0);
    rb_define_method(cSprite, "z=", nx_sprite_set_z, 1);
    rb_define_method(cSprite, "angle", nx_sprite_get_angle, 0);
    rb_define_method(cSprite, "angle=", nx_sprite_set_angle, 1);
    rb_define_method(cSprite, "scale_x", nx_sprite_get_scale_x, 0);
    rb_define_method(cSprite, "scale_x=", nx_sprite_set_scale_x, 1);
    rb_define_method(cSprite, "scale_y", nx_sprite_get_scale_y, 0);
    rb_define_method(cSprite, "scale_y=", nx_sprite_set_scale_y, 1);
    rb_define_method(cSprite, "center_x", nx_sprite_get_center_x, 0);
    rb_define_method(cSprite, "center_x=", nx_sprite_set_center_x, 1);
    rb_define_method(cSprite, "center_y", nx_sprite_get_center_y, 0);
    rb_define_method(cSprite, "center_y=", nx_sprite_set_center_y, 1);
    rb_define_method(cSprite, "alpha", nx_sprite_get_alpha, 0);
    rb_define_method(cSprite, "alpha=", nx_sprite_set_alpha, 1);
    rb_define_method(cSprite, "blend", nx_sprite_get_blend, 0);
    rb_define_method(cSprite, "blend=", nx_sprite_set_blend, 1);
    rb_define_method(cSprite, "image", nx_sprite_get_image, 0);
    rb_define_method(cSprite, "image=", nx_sprite_set_image, 1);
    rb_define_method(cSprite, "collision_enable", nx_sprite_get_collision_enable, 0);
    rb_define_method(cSprite, "collision_enable=", nx_sprite_set_collision_enable, 1);
    rb_define_method(cSprite, "vanish", nx_sprite_vanish, 0);
    rb_define_method(cSprite, "vanished?", nx_sprite_is_vanished, 0);
    rb_define_method(cSprite, "dispose", nx_sprite_dispose, 0);
    rb_define_method(cSprite, "disposed?", nx_sprite_is_disposed, 0);
}
