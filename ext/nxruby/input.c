#include <ruby.h>
#include <SDL.h>
#include <stdbool.h>
#include <string.h>
#include "input.h"
#include "window.h"

// ================================================================================
// [1] データ定義と内部状態
// ================================================================================

// --- 入力状態を管理する構造体 ---
typedef struct {
    // SDLからの生データ
    Uint8  raw_keys[SDL_NUM_SCANCODES];
    float  raw_mouse_x;
    float  raw_mouse_y;
    Uint32 raw_mouse_btn;

    // キーボード入力用
    Uint8  current_keys[SDL_NUM_SCANCODES];
    Uint8  prev_keys[SDL_NUM_SCANCODES];
    // イベントフラグ
    Uint8  pressed[SDL_NUM_SCANCODES]; 
    // オートリピート用の配列
    int    key_count[SDL_NUM_SCANCODES];    // 何フレーム押され続けているか
    int    key_wait[SDL_NUM_SCANCODES];     // リピート開始までのフレーム数
    int    key_interval[SDL_NUM_SCANCODES]; // リピート間隔フレーム数

    // マウス入力用
    float  mouse_x;
    float  mouse_y;
    int    mouse_wheel_pos;
    Uint32 current_mouse_btn;
    Uint32 prev_mouse_btn;
    // イベントフラグ
    Uint32 pressed_mouse_btn; 

    // ゲームパッド管理用
    SDL_GameController *gamepad;
    SDL_JoystickID gamepad_id;
    Uint8  current_pad_btns[SDL_CONTROLLER_BUTTON_MAX];
    Uint8  prev_pad_btns[SDL_CONTROLLER_BUTTON_MAX];
    Uint8  pressed_pad_btns[SDL_CONTROLLER_BUTTON_MAX];
    // オートリピート用の配列
    int    pad_count[SDL_CONTROLLER_BUTTON_MAX];        
    int    pad_wait[SDL_CONTROLLER_BUTTON_MAX];         
    int    pad_interval[SDL_CONTROLLER_BUTTON_MAX];     
} InputState;

// 実体の宣言
static InputState input_state;

// ================================================================================
// [2] 内部更新処理
// ================================================================================

// --- SDLから入力イベントを取得 ---
void nx_input_handle_event(const SDL_Event *event) {
    switch (event->type) {
        // キーボード: 押された
        case SDL_KEYDOWN:
            if (event->key.keysym.scancode < SDL_NUM_SCANCODES) {
                input_state.raw_keys[event->key.keysym.scancode] = 1;
                input_state.pressed[event->key.keysym.scancode] = 1;
            }
            break;
        // キーボード: 離された
        case SDL_KEYUP:
            if (event->key.keysym.scancode < SDL_NUM_SCANCODES) {
                input_state.raw_keys[event->key.keysym.scancode] = 0;
            }
            break;
        // マウス: 移動
        case SDL_MOUSEMOTION:
            input_state.raw_mouse_x = (float)event->motion.x;
            input_state.raw_mouse_y = (float)event->motion.y;
            break;
        //　マウス: ホイール
        case SDL_MOUSEWHEEL:
            input_state.mouse_wheel_pos += event->wheel.y;
            break;
        // マウス: ボタンが押された
        case SDL_MOUSEBUTTONDOWN:
            input_state.pressed_mouse_btn |= SDL_BUTTON(event->button.button);
            input_state.raw_mouse_btn = SDL_GetMouseState(NULL, NULL);
            break;
        // マウス: ボタンが離された
        case SDL_MOUSEBUTTONUP:
            input_state.raw_mouse_btn = SDL_GetMouseState(NULL, NULL);
            break;
        // ゲームパッド: 接続
        case SDL_CONTROLLERDEVICEADDED:
            if (!input_state.gamepad) {
                input_state.gamepad = SDL_GameControllerOpen(event->cdevice.which);
                if (input_state.gamepad) {
                    input_state.gamepad_id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(input_state.gamepad));
                    SDL_Log("[NXRuby] Gamepad connected: %s", SDL_GameControllerName(input_state.gamepad));
                }
            }
            break;
        // ゲームパッド: 切断
        case SDL_CONTROLLERDEVICEREMOVED:
            if (input_state.gamepad && event->cdevice.which == input_state.gamepad_id) {
                SDL_Log("[NXRuby] Gamepad disconnected.");
                SDL_GameControllerClose(input_state.gamepad);
                input_state.gamepad = NULL;
                input_state.gamepad_id = 0;
            }
            break;
        // ゲームパッド: ボタンが押された瞬間をキャッチ (高速入力対応)
        case SDL_CONTROLLERBUTTONDOWN:
            if (event->cbutton.button < SDL_CONTROLLER_BUTTON_MAX) {
                input_state.pressed_pad_btns[event->cbutton.button] = 1;
            }
            break;
    }
}

// --- 論理フレームの更新 ---
void nx_input_update(void) {
    // 現在のキー状態を「過去(prev)」にコピー
    memcpy(input_state.prev_keys, input_state.current_keys, sizeof(input_state.current_keys));
    input_state.prev_mouse_btn = input_state.current_mouse_btn;

    // キーボードの状態を決定
    for (int i = 0; i < SDL_NUM_SCANCODES; i++) {
        input_state.current_keys[i] = input_state.raw_keys[i] | input_state.pressed[i];

        if (input_state.current_keys[i]) {
            input_state.key_count[i]++;
        } else {
            input_state.key_count[i] = 0;
        }
        input_state.pressed[i] = 0;
    }

    // ゲームパッドボタンの更新
    if (input_state.gamepad) {
        for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++) {
            Uint8 raw = SDL_GameControllerGetButton(input_state.gamepad, (SDL_GameControllerButton)i) ? 1 : 0;
            input_state.current_pad_btns[i] = raw | input_state.pressed_pad_btns[i];

            if (input_state.current_pad_btns[i]) {
                input_state.pad_count[i]++;
            } else {
                input_state.pad_count[i] = 0;
            }
            input_state.pressed_pad_btns[i] = 0;
        }
    }

    // マウスの状態を決定
    input_state.current_mouse_btn = input_state.raw_mouse_btn | input_state.pressed_mouse_btn;
    input_state.mouse_x = input_state.raw_mouse_x;
    input_state.mouse_y = input_state.raw_mouse_y;

    // マウスのイベントフラグを掃除
    input_state.pressed_mouse_btn = 0;
}

// ================================================================================
// [3] ヘルパー関数
// ================================================================================

// アナログスティックの値を -1.0 〜 1.0 に丸めて取得
static float nx_get_pad_axis(SDL_GameControllerAxis axis) {
    if (!input_state.gamepad) return 0.0f;
    Sint16 val = SDL_GameControllerGetAxis(input_state.gamepad, axis);
    
    if (val < -32767) val = -32767;

    const float DEADZONE = 8000.0f;
    const float MAX_VAL = 32767.0f;

    if (val > -DEADZONE && val < DEADZONE) return 0.0f;
    
    if (val >= DEADZONE) {
        return (val - DEADZONE) / (MAX_VAL - DEADZONE);
    } else {
        return (val + DEADZONE) / (MAX_VAL - DEADZONE);
    }
}

// ================================================================================
// [4] Ruby API: キーボード入力 ＆ パッド統合入力
// ================================================================================

static VALUE nx_input_set_repeat(VALUE self, VALUE wait_val, VALUE interval_val) {
    int wait = NUM2INT(wait_val);
    int interval = NUM2INT(interval_val);
    
    for (int i = 0; i < SDL_NUM_SCANCODES; i++) {
        input_state.key_wait[i] = wait;
        input_state.key_interval[i] = interval;
    }
    for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++) {
        input_state.pad_wait[i] = wait;
        input_state.pad_interval[i] = interval;
    }
    return Qnil;
}

static VALUE nx_input_x(VALUE self) {
    float x = 0.0f;
    if (input_state.current_keys[SDL_SCANCODE_RIGHT]) x += 1.0f;
    if (input_state.current_keys[SDL_SCANCODE_LEFT])  x -= 1.0f;
    if (input_state.gamepad) {
        if (SDL_GameControllerGetButton(input_state.gamepad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) x += 1.0f;
        if (SDL_GameControllerGetButton(input_state.gamepad, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  x -= 1.0f;
        Sint16 axis = SDL_GameControllerGetAxis(input_state.gamepad, SDL_CONTROLLER_AXIS_LEFTX);
        if (axis < -8000 || axis > 8000) x += (float)axis / 32767.0f;
    }
    if (x < -1.0f) x = -1.0f;
    if (x > 1.0f)  x = 1.0f;
    return DBL2NUM(x);
}

static VALUE nx_input_y(VALUE self) {
    float y = 0.0f;
    if (input_state.current_keys[SDL_SCANCODE_DOWN]) y += 1.0f;
    if (input_state.current_keys[SDL_SCANCODE_UP])   y -= 1.0f;
    if (input_state.gamepad) {
        if (SDL_GameControllerGetButton(input_state.gamepad, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) y += 1.0f;
        if (SDL_GameControllerGetButton(input_state.gamepad, SDL_CONTROLLER_BUTTON_DPAD_UP))   y -= 1.0f;
        Sint16 axis = SDL_GameControllerGetAxis(input_state.gamepad, SDL_CONTROLLER_AXIS_LEFTY);
        if (axis < -8000 || axis > 8000) y += (float)axis / 32767.0f;
    }
    if (y < -1.0f) y = -1.0f;
    if (y > 1.0f)  y = 1.0f;
    return DBL2NUM(y);
}

// --- キーボード判定 ---
static VALUE nx_input_key_down(VALUE self, VALUE key_val) {
    int key = NUM2INT(key_val);
    if (key < 0 || key >= SDL_NUM_SCANCODES) return Qfalse;
    return input_state.current_keys[key] ? Qtrue : Qfalse;
}

static VALUE nx_input_key_push(VALUE self, VALUE key_val) {
    int key = NUM2INT(key_val);
    if (key < 0 || key >= SDL_NUM_SCANCODES) return Qfalse;
    int count = input_state.key_count[key];
    int wait = input_state.key_wait[key];
    int interval = input_state.key_interval[key];
    if (count == 1) return Qtrue;
    if (wait > 0 && interval > 0 && count > wait) {
        if (interval == 1 || (count - wait) % interval == 1) return Qtrue; 
    }
    return Qfalse;
}

static VALUE nx_input_key_release(VALUE self, VALUE key_val) {
    int key = NUM2INT(key_val);
    if (key < 0 || key >= SDL_NUM_SCANCODES) return Qfalse;
    return (!input_state.current_keys[key] && input_state.prev_keys[key]) ? Qtrue : Qfalse;
}

// --- ゲームパッド判定 ---
static VALUE nx_input_pad_down(VALUE self, VALUE btn_val) {
    int btn = NUM2INT(btn_val);
    if (!input_state.gamepad || btn < 0 || btn >= SDL_CONTROLLER_BUTTON_MAX) return Qfalse;
    return input_state.current_pad_btns[btn] ? Qtrue : Qfalse;
}

static VALUE nx_input_pad_push(VALUE self, VALUE btn_val) {
    int btn = NUM2INT(btn_val);
    if (!input_state.gamepad || btn < 0 || btn >= SDL_CONTROLLER_BUTTON_MAX) return Qfalse;
    int count = input_state.pad_count[btn];
    int wait = input_state.pad_wait[btn];
    int interval = input_state.pad_interval[btn];
    if (count == 1) return Qtrue;
    if (wait > 0 && interval > 0 && count > wait) {
        if (interval == 1 || (count - wait) % interval == 1) return Qtrue; 
    }
    return Qfalse;
}

static VALUE nx_input_pad_release(VALUE self, VALUE btn_val) {
    int btn = NUM2INT(btn_val);
    if (!input_state.gamepad || btn < 0 || btn >= SDL_CONTROLLER_BUTTON_MAX) return Qfalse;
    return (!input_state.current_pad_btns[btn] && input_state.prev_pad_btns[btn]) ? Qtrue : Qfalse;
}

static VALUE nx_input_pad_lx(VALUE self) { return DBL2NUM(nx_get_pad_axis(SDL_CONTROLLER_AXIS_LEFTX)); }
static VALUE nx_input_pad_ly(VALUE self) { return DBL2NUM(nx_get_pad_axis(SDL_CONTROLLER_AXIS_LEFTY)); }
static VALUE nx_input_pad_rx(VALUE self) { return DBL2NUM(nx_get_pad_axis(SDL_CONTROLLER_AXIS_RIGHTX)); }
static VALUE nx_input_pad_ry(VALUE self) { return DBL2NUM(nx_get_pad_axis(SDL_CONTROLLER_AXIS_RIGHTY)); }

// ================================================================================
// [5] Ruby API: マウス入力
// ================================================================================

static VALUE nx_input_set_mouse_enable(VALUE self, VALUE enable_val) {
    if (RTEST(enable_val)) SDL_ShowCursor(SDL_ENABLE); else SDL_ShowCursor(SDL_DISABLE);
    return enable_val;
}

static VALUE nx_input_set_mouse_pos(VALUE self, VALUE rx, VALUE ry) {
    nx_window_set_mouse_pos((float)NUM2DBL(rx), (float)NUM2DBL(ry));
    return Qnil;
}

static VALUE nx_input_mouse_x(VALUE self) { return DBL2NUM(input_state.mouse_x); }
static VALUE nx_input_mouse_y(VALUE self) { return DBL2NUM(input_state.mouse_y); }
static VALUE nx_input_mouse_wheel_pos(VALUE self) { return INT2NUM(input_state.mouse_wheel_pos); }

static VALUE nx_input_set_mouse_wheel_pos(VALUE self, VALUE pos_val) {
    input_state.mouse_wheel_pos = NUM2INT(pos_val);
    return pos_val;
}

static Uint32 nx_input_get_mouse_mask(int button) {
    switch (button) {
        case 0: return SDL_BUTTON_LMASK;
        case 1: return SDL_BUTTON_RMASK;
        case 2: return SDL_BUTTON_MMASK;
        default: return 0;
    }
}

static VALUE nx_input_mouse_down(VALUE self, VALUE btn_val) {
    Uint32 mask = nx_input_get_mouse_mask(NUM2INT(btn_val));
    if (mask == 0) return Qfalse;
    return (input_state.current_mouse_btn & mask) ? Qtrue : Qfalse;
}

static VALUE nx_input_mouse_push(VALUE self, VALUE btn_val) {
    Uint32 mask = nx_input_get_mouse_mask(NUM2INT(btn_val));
    if (mask == 0) return Qfalse;
    bool current = (input_state.current_mouse_btn & mask) != 0;
    bool prev    = (input_state.prev_mouse_btn & mask) != 0;
    return (current && !prev) ? Qtrue : Qfalse;
}

static VALUE nx_input_mouse_release(VALUE self, VALUE btn_val) {
    Uint32 mask = nx_input_get_mouse_mask(NUM2INT(btn_val));
    if (mask == 0) return Qfalse;
    bool current = (input_state.current_mouse_btn & mask) != 0;
    bool prev    = (input_state.prev_mouse_btn & mask) != 0;
    return (!current && prev) ? Qtrue : Qfalse;
}

// ================================================================================
// [6] 定数テーブル (キーボード ＆ パッド)
// ================================================================================

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
    {"K_SPACE", SDL_SCANCODE_SPACE}, {"K_BACKSPACE", SDL_SCANCODE_BACKSPACE},
    {"K_TAB", SDL_SCANCODE_TAB}, 
    {"K_LSHIFT", SDL_SCANCODE_LSHIFT}, {"K_RSHIFT", SDL_SCANCODE_RSHIFT}, 
    {"K_LCTRL", SDL_SCANCODE_LCTRL}, {"K_RCTRL", SDL_SCANCODE_RCTRL}, 
    {"K_LALT", SDL_SCANCODE_LALT}, {"K_RALT", SDL_SCANCODE_RALT}, 
    {"K_MINUS", SDL_SCANCODE_MINUS}, {"K_EQUALS", SDL_SCANCODE_EQUALS},
    {"K_LBRACKET", SDL_SCANCODE_LEFTBRACKET}, {"K_RBRACKET", SDL_SCANCODE_RIGHTBRACKET},
    {"K_SEMICOLON", SDL_SCANCODE_SEMICOLON}, {"K_APOSTROPHE", SDL_SCANCODE_APOSTROPHE},
    {"K_GRAVE", SDL_SCANCODE_GRAVE}, {"K_BACKSLASH", SDL_SCANCODE_BACKSLASH},
    {"K_COMMA", SDL_SCANCODE_COMMA}, {"K_PERIOD", SDL_SCANCODE_PERIOD},
    {"K_SLASH", SDL_SCANCODE_SLASH},
    {"K_INSERT", SDL_SCANCODE_INSERT}, {"K_DELETE", SDL_SCANCODE_DELETE},
    {"K_HOME", SDL_SCANCODE_HOME}, {"K_END", SDL_SCANCODE_END},
    {"K_PAGEUP", SDL_SCANCODE_PAGEUP}, {"K_PAGEDOWN", SDL_SCANCODE_PAGEDOWN},
    {"K_CAPSLOCK", SDL_SCANCODE_CAPSLOCK},
    {"K_NUMPAD0", SDL_SCANCODE_KP_0}, {"K_NUMPAD1", SDL_SCANCODE_KP_1},
    {"K_NUMPAD2", SDL_SCANCODE_KP_2}, {"K_NUMPAD3", SDL_SCANCODE_KP_3},
    {"K_NUMPAD4", SDL_SCANCODE_KP_4}, {"K_NUMPAD5", SDL_SCANCODE_KP_5},
    {"K_NUMPAD6", SDL_SCANCODE_KP_6}, {"K_NUMPAD7", SDL_SCANCODE_KP_7},
    {"K_NUMPAD8", SDL_SCANCODE_KP_8}, {"K_NUMPAD9", SDL_SCANCODE_KP_9},
    {"K_MULTIPLY", SDL_SCANCODE_KP_MULTIPLY}, {"K_ADD", SDL_SCANCODE_KP_PLUS},
    {"K_SUBTRACT", SDL_SCANCODE_KP_MINUS}, {"K_DECIMAL", SDL_SCANCODE_KP_PERIOD},
    {"K_DIVIDE", SDL_SCANCODE_KP_DIVIDE},
    {"K_F1", SDL_SCANCODE_F1}, {"K_F2", SDL_SCANCODE_F2}, {"K_F3", SDL_SCANCODE_F3},
    {"K_F4", SDL_SCANCODE_F4}, {"K_F5", SDL_SCANCODE_F5}, {"K_F6", SDL_SCANCODE_F6},
    {"K_F7", SDL_SCANCODE_F7}, {"K_F8", SDL_SCANCODE_F8}, {"K_F9", SDL_SCANCODE_F9},
    {"K_F10", SDL_SCANCODE_F10}, {"K_F11", SDL_SCANCODE_F11}, {"K_F12", SDL_SCANCODE_F12},
    {NULL, 0}
};

typedef struct {
    const char *name;
    SDL_GameControllerButton code;
} PadTable;

static const PadTable pad_table[] = {
    {"PAD_A", SDL_CONTROLLER_BUTTON_A},
    {"PAD_B", SDL_CONTROLLER_BUTTON_B},
    {"PAD_X", SDL_CONTROLLER_BUTTON_X},
    {"PAD_Y", SDL_CONTROLLER_BUTTON_Y},
    {"PAD_L1", SDL_CONTROLLER_BUTTON_LEFTSHOULDER},
    {"PAD_R1", SDL_CONTROLLER_BUTTON_RIGHTSHOULDER},
    {"PAD_UP", SDL_CONTROLLER_BUTTON_DPAD_UP},
    {"PAD_DOWN", SDL_CONTROLLER_BUTTON_DPAD_DOWN},
    {"PAD_LEFT", SDL_CONTROLLER_BUTTON_DPAD_LEFT},
    {"PAD_RIGHT", SDL_CONTROLLER_BUTTON_DPAD_RIGHT},
    {NULL, 0}
};

// ================================================================================
// [6] 初期化とメソッド登録
// ================================================================================

void nx_input_init(void) {
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);

    VALUE mInput = rb_define_module("Input");

    for (int i = 0; i < SDL_NUM_SCANCODES; i++) {
        input_state.key_wait[i] = 0; 
        input_state.key_interval[i] = 0;
    }
    for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++) {
        input_state.pad_wait[i] = 0; 
        input_state.pad_interval[i] = 0;
    }
    
    rb_define_const(mInput, "M_LBUTTON", INT2NUM(0));
    rb_define_const(mInput, "M_RBUTTON", INT2NUM(1));
    rb_define_const(mInput, "M_MBUTTON", INT2NUM(2));

    for (int i = 0; key_table[i].name; i++) rb_define_const(mInput, key_table[i].name, INT2NUM(key_table[i].code));
    for (int i = 0; pad_table[i].name; i++) rb_define_const(mInput, pad_table[i].name, INT2NUM(pad_table[i].code));

    rb_define_module_function(mInput, "set_repeat", nx_input_set_repeat, 2);
    rb_define_module_function(mInput, "x", nx_input_x, 0);
    rb_define_module_function(mInput, "y", nx_input_y, 0);
    
    rb_define_module_function(mInput, "key_down?", nx_input_key_down, 1);
    rb_define_module_function(mInput, "key_push?", nx_input_key_push, 1);
    rb_define_module_function(mInput, "key_release?", nx_input_key_release, 1);
    
    rb_define_module_function(mInput, "pad_down?", nx_input_pad_down, 1);
    rb_define_module_function(mInput, "pad_push?", nx_input_pad_push, 1);
    rb_define_module_function(mInput, "pad_release?", nx_input_pad_release, 1);
    rb_define_module_function(mInput, "pad_lx", nx_input_pad_lx, 0);
    rb_define_module_function(mInput, "pad_ly", nx_input_pad_ly, 0);
    rb_define_module_function(mInput, "pad_rx", nx_input_pad_rx, 0);
    rb_define_module_function(mInput, "pad_ry", nx_input_pad_ry, 0);

    rb_define_module_function(mInput, "mouse_enable=", nx_input_set_mouse_enable, 1);
    rb_define_module_function(mInput, "set_mouse_pos", nx_input_set_mouse_pos, 2);
    rb_define_module_function(mInput, "mouse_x", nx_input_mouse_x, 0);
    rb_define_module_function(mInput, "mouse_y", nx_input_mouse_y, 0);
    rb_define_module_function(mInput, "mouse_wheel_pos", nx_input_mouse_wheel_pos, 0);
    rb_define_module_function(mInput, "mouse_wheel_pos=", nx_input_set_mouse_wheel_pos, 1);
    rb_define_module_function(mInput, "mouse_down?", nx_input_mouse_down, 1);
    rb_define_module_function(mInput, "mouse_push?", nx_input_mouse_push, 1);
    rb_define_module_function(mInput, "mouse_release?", nx_input_mouse_release, 1);
}