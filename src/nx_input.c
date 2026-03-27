#include <mruby.h>
#include <SDL3/SDL.h>
#include "nx_input.h"

// ============================================================================
// [Data] 内部状態と定数テーブル
// ============================================================================

// --- 入力状態を管理する構造体 ---
typedef struct {
    // SDLからの生データ
    Uint8  raw_keys[SDL_SCANCODE_COUNT];
    float  raw_mouse_x;
    float  raw_mouse_y;
    Uint32 raw_mouse_btn;

    // キーボード入力用
    Uint8  current_keys[SDL_SCANCODE_COUNT];
    Uint8  prev_keys[SDL_SCANCODE_COUNT];

    // オートリピート用の配列
    int    key_count[SDL_SCANCODE_COUNT];    // 何フレーム押され続けているか
    int    key_wait[SDL_SCANCODE_COUNT];     // リピート開始までのフレーム数
    int    key_interval[SDL_SCANCODE_COUNT]; // リピート間隔フレーム数

    // マウス入力用
    float  mouse_x;
    float  mouse_y;
    Uint32 current_mouse_btn;
    Uint32 prev_mouse_btn;
} InputState;

// 実体の宣言
static InputState input_state;

// --- キーの名前とスキャンコードの構造体 ---
typedef struct {
    const char   *name;
    SDL_Scancode  code;
} KeyTable;

static const KeyTable key_table[] = {
    // アルファベット
    {"K_A", SDL_SCANCODE_A}, {"K_B", SDL_SCANCODE_B}, {"K_C", SDL_SCANCODE_C},
    {"K_D", SDL_SCANCODE_D}, {"K_E", SDL_SCANCODE_E}, {"K_F", SDL_SCANCODE_F},
    {"K_G", SDL_SCANCODE_G}, {"K_H", SDL_SCANCODE_H}, {"K_I", SDL_SCANCODE_I},
    {"K_J", SDL_SCANCODE_J}, {"K_K", SDL_SCANCODE_K}, {"K_L", SDL_SCANCODE_L},
    {"K_M", SDL_SCANCODE_M}, {"K_N", SDL_SCANCODE_N}, {"K_O", SDL_SCANCODE_O},
    {"K_P", SDL_SCANCODE_P}, {"K_Q", SDL_SCANCODE_Q}, {"K_R", SDL_SCANCODE_R},
    {"K_S", SDL_SCANCODE_S}, {"K_T", SDL_SCANCODE_T}, {"K_U", SDL_SCANCODE_U},
    {"K_V", SDL_SCANCODE_V}, {"K_W", SDL_SCANCODE_W}, {"K_X", SDL_SCANCODE_X},
    {"K_Y", SDL_SCANCODE_Y}, {"K_Z", SDL_SCANCODE_Z},
    
    // 数字
    {"K_0", SDL_SCANCODE_0}, {"K_1", SDL_SCANCODE_1}, {"K_2", SDL_SCANCODE_2},
    {"K_3", SDL_SCANCODE_3}, {"K_4", SDL_SCANCODE_4}, {"K_5", SDL_SCANCODE_5},
    {"K_6", SDL_SCANCODE_6}, {"K_7", SDL_SCANCODE_7}, {"K_8", SDL_SCANCODE_8},
    {"K_9", SDL_SCANCODE_9},

    // 方向キー
    {"K_UP", SDL_SCANCODE_UP}, {"K_DOWN", SDL_SCANCODE_DOWN},
    {"K_LEFT", SDL_SCANCODE_LEFT}, {"K_RIGHT", SDL_SCANCODE_RIGHT},
    
    // 主要コントロールキー
    {"K_RETURN", SDL_SCANCODE_RETURN}, {"K_ESCAPE", SDL_SCANCODE_ESCAPE},
    {"K_SPACE", SDL_SCANCODE_SPACE}, {"K_BACKSPACE", SDL_SCANCODE_BACKSPACE},
    {"K_TAB", SDL_SCANCODE_TAB}, 
    
    // 修飾キー
    {"K_LSHIFT", SDL_SCANCODE_LSHIFT}, {"K_RSHIFT", SDL_SCANCODE_RSHIFT}, 
    {"K_LCTRL", SDL_SCANCODE_LCTRL}, {"K_RCTRL", SDL_SCANCODE_RCTRL}, 
    {"K_LALT", SDL_SCANCODE_LALT}, {"K_RALT", SDL_SCANCODE_RALT}, 

    // 記号系
    {"K_MINUS", SDL_SCANCODE_MINUS}, {"K_EQUALS", SDL_SCANCODE_EQUALS},
    {"K_LBRACKET", SDL_SCANCODE_LEFTBRACKET}, {"K_RBRACKET", SDL_SCANCODE_RIGHTBRACKET},
    {"K_SEMICOLON", SDL_SCANCODE_SEMICOLON}, {"K_APOSTROPHE", SDL_SCANCODE_APOSTROPHE},
    {"K_GRAVE", SDL_SCANCODE_GRAVE}, {"K_BACKSLASH", SDL_SCANCODE_BACKSLASH},
    {"K_COMMA", SDL_SCANCODE_COMMA}, {"K_PERIOD", SDL_SCANCODE_PERIOD},
    {"K_SLASH", SDL_SCANCODE_SLASH},

    // ナビゲーション・特殊
    {"K_INSERT", SDL_SCANCODE_INSERT}, {"K_DELETE", SDL_SCANCODE_DELETE},
    {"K_HOME", SDL_SCANCODE_HOME}, {"K_END", SDL_SCANCODE_END},
    {"K_PAGEUP", SDL_SCANCODE_PAGEUP}, {"K_PAGEDOWN", SDL_SCANCODE_PAGEDOWN},
    {"K_CAPSLOCK", SDL_SCANCODE_CAPSLOCK},

    // テンキー
    {"K_NUMPAD0", SDL_SCANCODE_KP_0}, {"K_NUMPAD1", SDL_SCANCODE_KP_1},
    {"K_NUMPAD2", SDL_SCANCODE_KP_2}, {"K_NUMPAD3", SDL_SCANCODE_KP_3},
    {"K_NUMPAD4", SDL_SCANCODE_KP_4}, {"K_NUMPAD5", SDL_SCANCODE_KP_5},
    {"K_NUMPAD6", SDL_SCANCODE_KP_6}, {"K_NUMPAD7", SDL_SCANCODE_KP_7},
    {"K_NUMPAD8", SDL_SCANCODE_KP_8}, {"K_NUMPAD9", SDL_SCANCODE_KP_9},
    {"K_MULTIPLY", SDL_SCANCODE_KP_MULTIPLY}, {"K_ADD", SDL_SCANCODE_KP_PLUS},
    {"K_SUBTRACT", SDL_SCANCODE_KP_MINUS}, {"K_DECIMAL", SDL_SCANCODE_KP_PERIOD},
    {"K_DIVIDE", SDL_SCANCODE_KP_DIVIDE},

    // ファンクションキー
    {"K_F1", SDL_SCANCODE_F1}, {"K_F2", SDL_SCANCODE_F2}, {"K_F3", SDL_SCANCODE_F3},
    {"K_F4", SDL_SCANCODE_F4}, {"K_F5", SDL_SCANCODE_F5}, {"K_F6", SDL_SCANCODE_F6},
    {"K_F7", SDL_SCANCODE_F7}, {"K_F8", SDL_SCANCODE_F8}, {"K_F9", SDL_SCANCODE_F9},
    {"K_F10", SDL_SCANCODE_F10}, {"K_F11", SDL_SCANCODE_F11}, {"K_F12", SDL_SCANCODE_F12},
    
    // 終端マーク
    {NULL, 0}
};

// --- モジュール登録・初期化 ---
void nx_input_init(mrb_state *mrb) {
    struct RClass *Input = mrb_define_module(mrb, "Input");

    // マウスボタン定数の登録
    mrb_define_const(mrb, Input, "M_LBUTTON", mrb_int_value(mrb, 0));
    mrb_define_const(mrb, Input, "M_RBUTTON", mrb_int_value(mrb, 1));
    mrb_define_const(mrb, Input, "M_MBUTTON", mrb_int_value(mrb, 2));

    mrb_define_module_function(mrb, Input, "set_repeat", nx_input_set_repeat, MRB_ARGS_REQ(2));

    mrb_define_module_function(mrb, Input, "x", nx_input_x, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, Input, "y", nx_input_y, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, Input, "key_down?", nx_input_key_down, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, Input, "key_push?", nx_input_key_push, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, Input, "key_release?", nx_input_key_release, MRB_ARGS_REQ(1));

    mrb_define_module_function(mrb, Input, "mouse_x", nx_input_mouse_x, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, Input, "mouse_y", nx_input_mouse_y, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, Input, "mouse_down?", nx_input_mouse_down, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, Input, "mouse_push?", nx_input_mouse_push, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, Input, "mouse_release?", nx_input_mouse_release, MRB_ARGS_REQ(1));

    // キーの一括登録ループ
    for (int i = 0; key_table[i].name != NULL; i++) {
        mrb_define_const(mrb, Input, key_table[i].name, mrb_int_value(mrb, key_table[i].code));
    }
}

// ============================================================================
// ■ 更新処理
// ============================================================================

// --- SDLから物理的な最新状態を取得 ---
void nx_input_poll(void) {
    int numkeys;
    const bool *state = SDL_GetKeyboardState(&numkeys);
    if (numkeys > SDL_SCANCODE_COUNT) numkeys = SDL_SCANCODE_COUNT;
    
    // 生データ(raw)として保存するだけ
    memcpy(input_state.raw_keys, state, numkeys * sizeof(input_state.raw_keys[0]));
    input_state.raw_mouse_btn = SDL_GetMouseState(&input_state.raw_mouse_x, &input_state.raw_mouse_y);
}

// --- 論理フレームの更新 ---
void nx_input_update(void) {
    // 1. 現在のキー状態を「過去(prev)」にコピー
    memcpy(input_state.prev_keys, input_state.current_keys, sizeof(input_state.current_keys));
    input_state.prev_mouse_btn = input_state.current_mouse_btn;

    // 2. 「生データ(raw)」を「現在(current)」に反映
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

// ============================================================================
// ■ キーボード入力API
// ============================================================================

// オートリピートモードに変更
mrb_value nx_input_set_repeat(mrb_state *mrb, mrb_value self) {
    mrb_int wait, interval;
    mrb_get_args(mrb, "ii", &wait, &interval);
    
    for (int i = 0; i < SDL_SCANCODE_COUNT; i++) {
        input_state.key_wait[i] = (int)wait;
        input_state.key_interval[i] = (int)interval;
    }
    return mrb_nil_value();
}

// カーソルキーの入力をX座標で返す(-1, 0, 1)
mrb_value nx_input_x(mrb_state *mrb, mrb_value self) {
    int x = 0;
    if (input_state.current_keys[SDL_SCANCODE_RIGHT]) x += 1;
    if (input_state.current_keys[SDL_SCANCODE_LEFT])  x -= 1;
    return mrb_int_value(mrb, x);
}

// カーソルキーの入力をy座標で返す(-1, 0, 1)
mrb_value nx_input_y(mrb_state *mrb, mrb_value self) {
    int y = 0;
    if (input_state.current_keys[SDL_SCANCODE_DOWN]) y += 1;
    if (input_state.current_keys[SDL_SCANCODE_UP])   y -= 1;
    return mrb_int_value(mrb, y);
}

// キーが押されているあいだ真を返す
mrb_value nx_input_key_down(mrb_state *mrb, mrb_value self) {
    mrb_int key;
    mrb_get_args(mrb, "i", &key);
    if (key < 0 || key >= SDL_SCANCODE_COUNT) return mrb_false_value();
    return mrb_bool_value(input_state.current_keys[key]);
}

// キーが押された瞬間真を返す
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

// キーが離された瞬間真を返す
mrb_value nx_input_key_release(mrb_state *mrb, mrb_value self) {
    mrb_int key;
    mrb_get_args(mrb, "i", &key);
    if (key < 0 || key >= SDL_SCANCODE_COUNT) return mrb_false_value();
    return mrb_bool_value(!input_state.current_keys[key] && input_state.prev_keys[key]);
}

// ============================================================================
// ■ マウス入力API
// ============================================================================

// マウスのX座標を返す
mrb_value nx_input_mouse_x(mrb_state *mrb, mrb_value self) {
    return mrb_float_value(mrb, input_state.mouse_x);
}

// マウスのy座標を返す
mrb_value nx_input_mouse_y(mrb_state *mrb, mrb_value self) {
    return mrb_float_value(mrb, input_state.mouse_y);
}

// マウスボタン定数をSDLのマウスのマスクに変換するヘルパー
static Uint32 nx_input_get_mouse_mask(mrb_int button) {
    switch (button) {
        case 0: return SDL_BUTTON_LMASK; // M_LBUTTON
        case 1: return SDL_BUTTON_RMASK; // M_RBUTTON
        case 2: return SDL_BUTTON_MMASK; // M_MBUTTON
        default: return 0;
    }
}

// マウスのボタンが押されているあいだ真を返す
mrb_value nx_input_mouse_down(mrb_state *mrb, mrb_value self) {
    mrb_int button;
    mrb_get_args(mrb, "i", &button);
    
    Uint32 button_mask= nx_input_get_mouse_mask(button);
    if (button_mask == 0) return mrb_false_value();

    return mrb_bool_value((input_state.current_mouse_btn & button_mask) != 0);
}

// マウスのボタンが押された瞬間真を返す
mrb_value nx_input_mouse_push(mrb_state *mrb, mrb_value self) {
    mrb_int button;
    mrb_get_args(mrb, "i", &button);
    
    Uint32 button_mask = nx_input_get_mouse_mask(button);
    if (button_mask == 0) return mrb_false_value();

    bool current = (input_state.current_mouse_btn & button_mask) != 0;
    bool prev    = (input_state.prev_mouse_btn & button_mask) != 0;
    return mrb_bool_value(current && !prev);
}

// マウスのボタンが離された瞬間真を返す
mrb_value nx_input_mouse_release(mrb_state *mrb, mrb_value self) {
    mrb_int button;
    mrb_get_args(mrb, "i", &button);
    
    Uint32 button_mask = nx_input_get_mouse_mask(button);
    if (button_mask == 0) return mrb_false_value();

    bool current = (input_state.current_mouse_btn & button_mask) != 0;
    bool prev    = (input_state.prev_mouse_btn & button_mask) != 0;
    return mrb_bool_value(!current && prev); // 現在オフかつ前回オン
}