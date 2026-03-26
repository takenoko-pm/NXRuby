#include <mruby.h>
#include <SDL3/SDL.h>
#include "input.h"

// 入力状態を管理する構造体
typedef struct {
    // SDLからの生データ
    bool   raw_keys[SDL_SCANCODE_COUNT];
    float  raw_mouse_x;
    float  raw_mouse_y;
    Uint32 raw_mouse_btn;

    bool   current_keys[SDL_SCANCODE_COUNT];
    bool   prev_keys[SDL_SCANCODE_COUNT];

    // オートリピート用の配列
    int    key_count[SDL_SCANCODE_COUNT];    // 何フレーム押され続けているか
    int    key_wait[SDL_SCANCODE_COUNT];     // リピート開始までのフレーム数
    int    key_interval[SDL_SCANCODE_COUNT]; // リピート間隔フレーム数

    float  mouse_x;
    float  mouse_y;
    Uint32 current_mouse_btn;
    Uint32 prev_mouse_btn;
} InputState;

// 実体の宣言
static InputState input_state;

// キーの名前とスキャンコードをペアにする構造体
typedef struct {
    const char   *name;
    SDL_Scancode  code;
} KeyTable;

static const KeyTable key_table[] = {
    {"K_A", SDL_SCANCODE_A}, {"K_B", SDL_SCANCODE_B}, {"K_C", SDL_SCANCODE_C},
    {"K_D", SDL_SCANCODE_D}, {"K_E", SDL_SCANCODE_E}, {"K_F", SDL_SCANCODE_F},
    {"K_G", SDL_SCANCODE_G}, {"K_H", SDL_SCANCODE_H}, {"K_I", SDL_SCANCODE_I},
    {"K_J", SDL_SCANCODE_J}, {"K_K", SDL_SCANCODE_K}, {"K_L", SDL_SCANCODE_L},
    {"K_M", SDL_SCANCODE_M}, {"K_N", SDL_SCANCODE_N}, {"K_O", SDL_SCANCODE_O},
    {"K_P", SDL_SCANCODE_P}, {"K_Q", SDL_SCANCODE_Q}, {"K_R", SDL_SCANCODE_R},
    {"K_S", SDL_SCANCODE_S}, {"K_T", SDL_SCANCODE_T}, {"K_U", SDL_SCANCODE_U},
    {"K_V", SDL_SCANCODE_V}, {"K_W", SDL_SCANCODE_W}, {"K_X", SDL_SCANCODE_X},
    {"K_Y", SDL_SCANCODE_Y}, {"K_Z", SDL_SCANCODE_Z},
    
    {"K_0", SDL_SCANCODE_0}, {"K_1", SDL_SCANCODE_1}, {"K_2", SDL_SCANCODE_2},
    {"K_3", SDL_SCANCODE_3}, {"K_4", SDL_SCANCODE_4}, {"K_5", SDL_SCANCODE_5},
    {"K_6", SDL_SCANCODE_6}, {"K_7", SDL_SCANCODE_7}, {"K_8", SDL_SCANCODE_8},
    {"K_9", SDL_SCANCODE_9},

    {"K_UP", SDL_SCANCODE_UP}, {"K_DOWN", SDL_SCANCODE_DOWN},
    {"K_LEFT", SDL_SCANCODE_LEFT}, {"K_RIGHT", SDL_SCANCODE_RIGHT},
    
    {"K_RETURN", SDL_SCANCODE_RETURN}, {"K_ESCAPE", SDL_SCANCODE_ESCAPE},
    {"K_SPACE", SDL_SCANCODE_SPACE}, {"K_LSHIFT", SDL_SCANCODE_LSHIFT},
    {"K_RSHIFT", SDL_SCANCODE_RSHIFT}, {"K_LCTRL", SDL_SCANCODE_LCTRL},
    {"K_RCTRL", SDL_SCANCODE_RCTRL}, {"K_LALT", SDL_SCANCODE_LALT},
    {"K_RALT", SDL_SCANCODE_RALT}, {"K_TAB", SDL_SCANCODE_TAB},

    {"K_F1", SDL_SCANCODE_F1}, {"K_F2", SDL_SCANCODE_F2}, {"K_F3", SDL_SCANCODE_F3},
    {"K_F4", SDL_SCANCODE_F4}, {"K_F5", SDL_SCANCODE_F5}, {"K_F6", SDL_SCANCODE_F6},
    {"K_F7", SDL_SCANCODE_F7}, {"K_F8", SDL_SCANCODE_F8}, {"K_F9", SDL_SCANCODE_F9},
    {"K_F10", SDL_SCANCODE_F10}, {"K_F11", SDL_SCANCODE_F11}, {"K_F12", SDL_SCANCODE_F12},
    
    {NULL, 0} // 終端マーク
};

// --- モジュール登録・初期化 ---
void nx_input_init(mrb_state *mrb) {
    struct RClass *Input = mrb_define_module(mrb, "Input");

    mrb_define_module_function(mrb, Input, "set_repeat", nx_input_set_repeat, MRB_ARGS_REQ(2));

    mrb_define_module_function(mrb, Input, "x", nx_input_x, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, Input, "y", nx_input_y, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, Input, "key_down?", nx_input_key_down, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, Input, "key_push?", nx_input_key_push, MRB_ARGS_REQ(1));
    
    mrb_define_module_function(mrb, Input, "mouse_x", nx_input_mouse_x, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, Input, "mouse_y", nx_input_mouse_y, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, Input, "mouse_down?", nx_input_mouse_down, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, Input, "mouse_push?", nx_input_mouse_push, MRB_ARGS_NONE());

    // キーの一括登録ループ
    for (int i = 0; key_table[i].name != NULL; i++) {
        mrb_define_const(mrb, Input, key_table[i].name, mrb_int_value(mrb, key_table[i].code));
    }
}

// --- 更新処理 ---
// --- SDLから物理的な最新状態を取得 ---
void nx_input_poll(void) {
    int numkeys;
    const bool *state = SDL_GetKeyboardState(&numkeys);
    if (numkeys > SDL_SCANCODE_COUNT) numkeys = SDL_SCANCODE_COUNT;
    
    // 生データ(raw)として保存するだけ
    memcpy(input_state.raw_keys, state, numkeys * sizeof(bool));
    input_state.raw_mouse_btn = SDL_GetMouseState(&input_state.raw_mouse_x, &input_state.raw_mouse_y);
}

// --- 論理フレームの更新 ---
void nx_input_update(void) {
    // 1. 現在のキー状態を「過去」にコピー
    memcpy(input_state.prev_keys, input_state.current_keys, sizeof(input_state.current_keys));
    input_state.prev_mouse_btn = input_state.current_mouse_btn;

    // 2. 「生データ(raw)」を「現在(current)」に反映する
    memcpy(input_state.current_keys, input_state.raw_keys, sizeof(input_state.raw_keys));
    input_state.current_mouse_btn = input_state.raw_mouse_btn;
    input_state.mouse_x = input_state.raw_mouse_x;
    input_state.mouse_y = input_state.raw_mouse_y;

    // 3. 押されていればカウントアップ、離されたら0にリセット
    for (int i = 0; i < SDL_SCANCODE_COUNT; i++) {
        if (input_state.current_keys[i]) {
            input_state.key_count[i]++;
        } else {
            input_state.key_count[i] = 0;
        }
    }
}

// --- キーボード ---
mrb_value nx_input_set_repeat(mrb_state *mrb, mrb_value self) {
    mrb_int wait, interval;
    mrb_get_args(mrb, "ii", &wait, &interval);
    
    for (int i = 0; i < SDL_SCANCODE_COUNT; i++) {
        input_state.key_wait[i] = (int)wait;
        input_state.key_interval[i] = (int)interval;
    }
    return mrb_nil_value();
}

mrb_value nx_input_x(mrb_state *mrb, mrb_value self) {
    int x = 0;
    if (input_state.current_keys[SDL_SCANCODE_RIGHT]) x += 1;
    if (input_state.current_keys[SDL_SCANCODE_LEFT])  x -= 1;
    return mrb_int_value(mrb, x);
}

mrb_value nx_input_y(mrb_state *mrb, mrb_value self) {
    int y = 0;
    if (input_state.current_keys[SDL_SCANCODE_DOWN]) y += 1;
    if (input_state.current_keys[SDL_SCANCODE_UP])   y -= 1;
    return mrb_int_value(mrb, y);
}

mrb_value nx_input_key_down(mrb_state *mrb, mrb_value self) {
    mrb_int key;
    mrb_get_args(mrb, "i", &key);
    if (key < 0 || key >= SDL_SCANCODE_COUNT) return mrb_false_value();
    return mrb_bool_value(input_state.current_keys[key]);
}

mrb_value nx_input_key_push(mrb_state *mrb, mrb_value self) {
    mrb_int key;
    mrb_get_args(mrb, "i", &key);
    if (key < 0 || key >= SDL_SCANCODE_COUNT) return mrb_false_value();

    int count = input_state.key_count[key];
    int wait = input_state.key_wait[key];
    int interval = input_state.key_interval[key];

    // 1. 押した最初の1フレーム目は true
    if (count == 1) return mrb_true_value();

    // 2. オートリピート設定が有効(waitとintervalが1以上)で、waitを超えている場合
    if (wait > 0 && interval > 0 && count > wait) {
        if ((count - wait) % interval == 0) {
            return mrb_true_value(); // インターバルごとに true
        }
    }

    return mrb_false_value();
}

// --- マウス ---
mrb_value nx_input_mouse_x(mrb_state *mrb, mrb_value self) {
    return mrb_float_value(mrb, input_state.mouse_x);
}

mrb_value nx_input_mouse_y(mrb_state *mrb, mrb_value self) {
    return mrb_float_value(mrb, input_state.mouse_y);
}

mrb_value nx_input_mouse_down(mrb_state *mrb, mrb_value self) {
    return mrb_bool_value((input_state.current_mouse_btn & SDL_BUTTON_LMASK) != 0);
}

mrb_value nx_input_mouse_push(mrb_state *mrb, mrb_value self) {
    bool current = (input_state.current_mouse_btn & SDL_BUTTON_LMASK) != 0;
    bool prev    = (input_state.prev_mouse_btn & SDL_BUTTON_LMASK) != 0;
    return mrb_bool_value(current && !prev);
}
