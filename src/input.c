#include <mruby.h>
#include <SDL3/SDL.h>
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
    Uint8  raw_keys[SDL_SCANCODE_COUNT];
    float  raw_mouse_x;
    float  raw_mouse_y;
    Uint32 raw_mouse_btn;

    // キーボード入力用
    Uint8  current_keys[SDL_SCANCODE_COUNT];
    Uint8  prev_keys[SDL_SCANCODE_COUNT];
    // イベントフラグ
    Uint8  pressed[SDL_SCANCODE_COUNT]; 
    // オートリピート用の配列
    int    key_count[SDL_SCANCODE_COUNT];    // 何フレーム押され続けているか
    int    key_wait[SDL_SCANCODE_COUNT];     // リピート開始までのフレーム数
    int    key_interval[SDL_SCANCODE_COUNT]; // リピート間隔フレーム数

    // マウス入力用
    float  mouse_x;
    float  mouse_y;
    int    mouse_wheel_pos;
    Uint32 current_mouse_btn;
    Uint32 prev_mouse_btn;
    // イベントフラグ
    Uint32 pressed_mouse_btn; 

    // ゲームパッド管理用
    SDL_Gamepad    *gamepad;
    SDL_JoystickID gamepad_id;
    Uint8  current_pad_btns[SDL_GAMEPAD_BUTTON_COUNT];
    Uint8  prev_pad_btns[SDL_GAMEPAD_BUTTON_COUNT];
    Uint8  pressed_pad_btns[SDL_GAMEPAD_BUTTON_COUNT];
    // オートリピート用の配列
    int    pad_count[SDL_GAMEPAD_BUTTON_COUNT];        // 何フレーム押され続けているか
    int    pad_wait[SDL_GAMEPAD_BUTTON_COUNT];         // リピート開始までのフレーム数
    int    pad_interval[SDL_GAMEPAD_BUTTON_COUNT];     // リピート間隔フレーム数

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
        case SDL_EVENT_KEY_DOWN:
            if (event->key.scancode < SDL_SCANCODE_COUNT) {
                input_state.raw_keys[event->key.scancode] = 1;
                input_state.pressed[event->key.scancode] = 1;
            }
            break;
        // キーボード: 離された
        case SDL_EVENT_KEY_UP:
            if (event->key.scancode < SDL_SCANCODE_COUNT) {
                input_state.raw_keys[event->key.scancode] = 0;
            }
            break;
        // マウス: 移動
        case SDL_EVENT_MOUSE_MOTION:
            input_state.raw_mouse_x = event->motion.x;
            input_state.raw_mouse_y = event->motion.y;
            break;
        //　マウス: ホイール
        case SDL_EVENT_MOUSE_WHEEL:
            // SDL3では event->wheel.y は float なので int にキャストして加算
            input_state.mouse_wheel_pos += (int)event->wheel.y;
            break;
        // マウス: ボタンが押された
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            input_state.pressed_mouse_btn |= SDL_BUTTON_MASK(event->button.button);
            input_state.raw_mouse_btn = SDL_GetMouseState(NULL, NULL);
            break;
        // マウス: ボタンが離された
        case SDL_EVENT_MOUSE_BUTTON_UP:
            input_state.raw_mouse_btn = SDL_GetMouseState(NULL, NULL);
            break;
        // ゲームパッド: 接続
        case SDL_EVENT_GAMEPAD_ADDED:
            if (!input_state.gamepad) {
                input_state.gamepad = SDL_OpenGamepad(event->gdevice.which);
                if (input_state.gamepad) {
                    input_state.gamepad_id = SDL_GetGamepadID(input_state.gamepad);
                    SDL_Log("[NXRuby] Gamepad connected: %s", SDL_GetGamepadName(input_state.gamepad));
                }
            }
            break;
        // ゲームパッド: 切断
        case SDL_EVENT_GAMEPAD_REMOVED:
            if (input_state.gamepad && event->gdevice.which == input_state.gamepad_id) {
                SDL_Log("[NXRuby] Gamepad disconnected.");
                SDL_CloseGamepad(input_state.gamepad);
                input_state.gamepad = NULL;
                input_state.gamepad_id = 0;
            }
            break;
        // ゲームパッド: ボタンが押された瞬間をキャッチ (高速入力対応)
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            if (event->gbutton.button < SDL_GAMEPAD_BUTTON_COUNT) {
                input_state.pressed_pad_btns[event->gbutton.button] = 1;
            }
            break;
    }
}

// --- 論理フレームの更新 ---
void nx_input_update(void) {
    // 現在のキー状態を「過去(prev)」にコピー
    memcpy(input_state.prev_keys, input_state.current_keys, sizeof(input_state.current_keys));
    input_state.prev_mouse_btn = input_state.current_mouse_btn;

    // キーボードの状態を決定（今物理的に押されているか、このフレームで一瞬でも押されたらON）
    for (int i = 0; i < SDL_SCANCODE_COUNT; i++) {
        // 今現在物理的に押されている(raw)か、このフレーム中に一瞬でも押された(pressed)なら「ON(1)」にする
        input_state.current_keys[i] = input_state.raw_keys[i] | input_state.pressed[i];

        // 押されていればカウントアップ、離されたら0にリセット
        if (input_state.current_keys[i]) {
            input_state.key_count[i]++;
        } else {
            input_state.key_count[i] = 0;
        }

        // キーボードのイベントフラグを掃除
        input_state.pressed[i] = 0;
    }

    // ゲームパッドボタンの更新
    if (input_state.gamepad) {
        for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; i++) {
            // 今物理的に押されているか取得
            Uint8 raw = SDL_GetGamepadButton(input_state.gamepad, (SDL_GamepadButton)i) ? 1 : 0;
            
            // 物理押し OR フレーム間の瞬間押し
            input_state.current_pad_btns[i] = raw | input_state.pressed_pad_btns[i];

            // 押されていればカウントアップ、離されたらリセット
            if (input_state.current_pad_btns[i]) {
                input_state.pad_count[i]++;
            } else {
                input_state.pad_count[i] = 0;
            }

            // フラグの掃除
            input_state.pressed_pad_btns[i] = 0;
        }
    }

    // マウスの状態を決定（今物理的に押されているか、このフレームで一瞬でも押されたらON）
    input_state.current_mouse_btn = input_state.raw_mouse_btn | input_state.pressed_mouse_btn;
    input_state.mouse_x = input_state.raw_mouse_x;
    input_state.mouse_y = input_state.raw_mouse_y;

    // マウスのイベントフラグを掃除
    input_state.pressed_mouse_btn = 0;
}

// ================================================================================
// [3] ヘルパー関数
// ================================================================================

// アナログスティックの値を -1.0 〜 1.0 に丸めて取得（デッドゾーン設定付き）
static float nx_get_pad_axis(SDL_GamepadAxis axis) {
    if (!input_state.gamepad) return 0.0f;
    Sint16 val = SDL_GetGamepadAxis(input_state.gamepad, axis);
    
    // SDL3の最小値 (-32768) が来た場合は -32767 にクランプする
    if (val < -32767) val = -32767;

    const float DEADZONE = 8000.0f;
    const float MAX_VAL = 32767.0f;

    // 約24%のデッドゾーン
    if (val > -DEADZONE && val < DEADZONE) return 0.0f;
    
    // デッドゾーンを超えた分を 0.0 〜 1.0 に再マッピング（ジャンプ現象を防ぐ）
    if (val >= DEADZONE) {
        return (val - DEADZONE) / (MAX_VAL - DEADZONE);
    } else {
        return (val + DEADZONE) / (MAX_VAL - DEADZONE); // 負の方向
    }
}

// ================================================================================
// [4] Ruby API: キーボード入力 ＆ パッド統合入力
// ================================================================================

// オートリピートモードに変更
static mrb_value nx_input_set_repeat(mrb_state *mrb, mrb_value self) {
    mrb_int wait, interval;
    mrb_get_args(mrb, "ii", &wait, &interval);
    
    for (int i = 0; i < SDL_SCANCODE_COUNT; i++) {
        input_state.key_wait[i] = (int)wait;
        input_state.key_interval[i] = (int)interval;
    }

    for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; i++) {
        input_state.pad_wait[i] = (int)wait;
        input_state.pad_interval[i] = (int)interval;
    }

    return mrb_nil_value();
}

// カーソルキー/パッドの入力をX座標で返す(-1, 0, 1)
static mrb_value nx_input_x(mrb_state *mrb, mrb_value self) {
    float x = 0.0f;
    
    // 1. キーボード
    if (input_state.current_keys[SDL_SCANCODE_RIGHT]) x += 1.0f;
    if (input_state.current_keys[SDL_SCANCODE_LEFT])  x -= 1.0f;

    // 2. ゲームパッド
    if (input_state.gamepad) {
        if (SDL_GetGamepadButton(input_state.gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) x += 1.0f;
        if (SDL_GetGamepadButton(input_state.gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT))  x -= 1.0f;

        Sint16 axis = SDL_GetGamepadAxis(input_state.gamepad, SDL_GAMEPAD_AXIS_LEFTX);
        if (axis < -8000 || axis > 8000) {
            x += (float)axis / 32767.0f;
        }
    }

    // クランプ処理
    if (x < -1.0f) x = -1.0f;
    if (x > 1.0f)  x = 1.0f;

    return mrb_float_value(mrb, x);
}

// カーソルキー/パッドの入力をy座標で返す(-1, 0, 1)
static mrb_value nx_input_y(mrb_state *mrb, mrb_value self) {
    float y = 0.0f;
    
    // 1. キーボード
    if (input_state.current_keys[SDL_SCANCODE_DOWN]) y += 1.0f;
    if (input_state.current_keys[SDL_SCANCODE_UP])   y -= 1.0f;

    // 2. ゲームパッド
    if (input_state.gamepad) {
        if (SDL_GetGamepadButton(input_state.gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) y += 1.0f;
        if (SDL_GetGamepadButton(input_state.gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP))   y -= 1.0f;

        Sint16 axis = SDL_GetGamepadAxis(input_state.gamepad, SDL_GAMEPAD_AXIS_LEFTY);
        if (axis < -8000 || axis > 8000) {
            y += (float)axis / 32767.0f;
        }
    }

    // クランプ処理
    if (y < -1.0f) y = -1.0f;
    if (y > 1.0f)  y = 1.0f;

    return mrb_float_value(mrb, y);
}

// --- キーボード判定 ---
// キーが押されているあいだ真を返す
static mrb_value nx_input_key_down(mrb_state *mrb, mrb_value self) {
    mrb_int key;
    mrb_get_args(mrb, "i", &key);
    if (key < 0 || key >= SDL_SCANCODE_COUNT) return mrb_false_value();
    return mrb_bool_value(input_state.current_keys[key]);
}

// キーが押された瞬間真を返す（オートリピート対応）
static mrb_value nx_input_key_push(mrb_state *mrb, mrb_value self) {
    mrb_int key;
    mrb_get_args(mrb, "i", &key);
    if (key < 0 || key >= SDL_SCANCODE_COUNT) return mrb_false_value();

    int count = input_state.key_count[key];
    int wait = input_state.key_wait[key];
    int interval = input_state.key_interval[key];

    // 押した最初の1フレーム目は true
    if (count == 1) return mrb_true_value();

    // オートリピート設定が有効(waitとintervalが1以上)で、waitを超えている場合
    if (wait > 0 && interval > 0 && count > wait) {
        // waitを超過したあとのフレーム数が、intervalの倍数に合致した瞬間だけtrue
        if (interval == 1 || (count - wait) % interval == 1) {
            return mrb_true_value(); 
        }
    }

    return mrb_false_value();
}

// キーが離された瞬間真を返す
static mrb_value nx_input_key_release(mrb_state *mrb, mrb_value self) {
    mrb_int key;
    mrb_get_args(mrb, "i", &key);
    if (key < 0 || key >= SDL_SCANCODE_COUNT) return mrb_false_value();
    return mrb_bool_value(!input_state.current_keys[key] && input_state.prev_keys[key]);
}

// --- ゲームパッド判定 ---
// キーが押されているあいだ真を返す
static mrb_value nx_input_pad_down(mrb_state *mrb, mrb_value self) {
    mrb_int btn; mrb_get_args(mrb, "i", &btn);
    if (!input_state.gamepad || btn < 0 || btn >= SDL_GAMEPAD_BUTTON_COUNT) return mrb_false_value();
    return mrb_bool_value(input_state.current_pad_btns[btn]);
}

// パッドが押された瞬間真を返す（オートリピート対応）
static mrb_value nx_input_pad_push(mrb_state *mrb, mrb_value self) {
    mrb_int btn; mrb_get_args(mrb, "i", &btn);
    if (!input_state.gamepad || btn < 0 || btn >= SDL_GAMEPAD_BUTTON_COUNT) return mrb_false_value();

    int count = input_state.pad_count[btn];
    int wait = input_state.pad_wait[btn];
    int interval = input_state.pad_interval[btn];

    // 押した最初の1フレーム目は true
    if (count == 1) return mrb_true_value();

    // オートリピート判定
    if (wait > 0 && interval > 0 && count > wait) {
        if (interval == 1 || (count - wait) % interval == 1) {
            return mrb_true_value(); 
        }
    }

    return mrb_false_value();
}

// パッドが離された瞬間真を返す
static mrb_value nx_input_pad_release(mrb_state *mrb, mrb_value self) {
    mrb_int btn; mrb_get_args(mrb, "i", &btn);
    if (!input_state.gamepad || btn < 0 || btn >= SDL_GAMEPAD_BUTTON_COUNT) return mrb_false_value();
    return mrb_bool_value(!input_state.current_pad_btns[btn] && input_state.prev_pad_btns[btn]);
}

// --- アナログスティック ---
static mrb_value nx_input_pad_lx(mrb_state *mrb, mrb_value self) { return mrb_float_value(mrb, nx_get_pad_axis(SDL_GAMEPAD_AXIS_LEFTX)); }
static mrb_value nx_input_pad_ly(mrb_state *mrb, mrb_value self) { return mrb_float_value(mrb, nx_get_pad_axis(SDL_GAMEPAD_AXIS_LEFTY)); }
static mrb_value nx_input_pad_rx(mrb_state *mrb, mrb_value self) { return mrb_float_value(mrb, nx_get_pad_axis(SDL_GAMEPAD_AXIS_RIGHTX)); }
static mrb_value nx_input_pad_ry(mrb_state *mrb, mrb_value self) { return mrb_float_value(mrb, nx_get_pad_axis(SDL_GAMEPAD_AXIS_RIGHTY)); }

// ================================================================================
// [5] Ruby API: マウス入力
// ================================================================================

// マウスカーソルの表示/非表示を切り替える
static mrb_value nx_input_set_mouse_enable(mrb_state *mrb, mrb_value self) {
    mrb_bool enable;
    mrb_get_args(mrb, "b", &enable);
    
    if (enable) {
        SDL_ShowCursor(); // SDL3標準の表示関数
    } else {
        SDL_HideCursor(); // SDL3標準の非表示関数
    }
    
    return mrb_bool_value(enable);
}

// マウスの座標を強制的に変更する
static mrb_value nx_input_set_mouse_pos(mrb_state *mrb, mrb_value self) {
    mrb_float x, y;
    // 引数として x, y を受け取る
    mrb_get_args(mrb, "ff", &x, &y);
    
    // window.c の関数を呼び出して実際にマウスを動かす
    nx_window_set_mouse_pos((float)x, (float)y);
    
    return mrb_nil_value();
}

// マウスのX座標を返す
static mrb_value nx_input_mouse_x(mrb_state *mrb, mrb_value self) {
    return mrb_float_value(mrb, input_state.mouse_x);
}

// マウスのy座標を返す
static mrb_value nx_input_mouse_y(mrb_state *mrb, mrb_value self) {
    return mrb_float_value(mrb, input_state.mouse_y);
}

// マウスホイールの位置を返す
static mrb_value nx_input_mouse_wheel_pos(mrb_state *mrb, mrb_value self) {
    return mrb_int_value(mrb, input_state.mouse_wheel_pos);
}

// マウスホイールの位置を強制的に上書きする
static mrb_value nx_input_set_mouse_wheel_pos(mrb_state *mrb, mrb_value self) {
    mrb_int pos;
    mrb_get_args(mrb, "i", &pos);
    input_state.mouse_wheel_pos = (int)pos;
    return mrb_int_value(mrb, pos);
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
static mrb_value nx_input_mouse_down(mrb_state *mrb, mrb_value self) {
    mrb_int button;
    mrb_get_args(mrb, "i", &button);
    
    Uint32 button_mask= nx_input_get_mouse_mask(button);
    if (button_mask == 0) return mrb_false_value();

    return mrb_bool_value((input_state.current_mouse_btn & button_mask) != 0);
}

// マウスのボタンが押された瞬間真を返す
static mrb_value nx_input_mouse_push(mrb_state *mrb, mrb_value self) {
    mrb_int button;
    mrb_get_args(mrb, "i", &button);
    
    Uint32 button_mask = nx_input_get_mouse_mask(button);
    if (button_mask == 0) return mrb_false_value();

    bool current = (input_state.current_mouse_btn & button_mask) != 0;
    bool prev    = (input_state.prev_mouse_btn & button_mask) != 0;
    return mrb_bool_value(current && !prev);
}

// マウスのボタンが離された瞬間真を返す
static mrb_value nx_input_mouse_release(mrb_state *mrb, mrb_value self) {
    mrb_int button;
    mrb_get_args(mrb, "i", &button);
    
    Uint32 button_mask = nx_input_get_mouse_mask(button);
    if (button_mask == 0) return mrb_false_value();

    bool current = (input_state.current_mouse_btn & button_mask) != 0;
    bool prev    = (input_state.prev_mouse_btn & button_mask) != 0;
    return mrb_bool_value(!current && prev); // 現在オフかつ前回オン
}

// ================================================================================
// [6] 定数テーブル (キーボード ＆ パッド)
// ================================================================================

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

typedef struct {
    const char *name;
    SDL_GamepadButton code;
} PadTable;

// ゲームパッドのボタン定数テーブル
static const PadTable pad_table[] = {
    {"PAD_A", SDL_GAMEPAD_BUTTON_SOUTH},
    {"PAD_B", SDL_GAMEPAD_BUTTON_EAST},
    {"PAD_X", SDL_GAMEPAD_BUTTON_WEST},
    {"PAD_Y", SDL_GAMEPAD_BUTTON_NORTH},
    {"PAD_L1", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
    {"PAD_R1", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
    {"PAD_UP", SDL_GAMEPAD_BUTTON_DPAD_UP},
    {"PAD_DOWN", SDL_GAMEPAD_BUTTON_DPAD_DOWN},
    {"PAD_LEFT", SDL_GAMEPAD_BUTTON_DPAD_LEFT},
    {"PAD_RIGHT", SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
    {NULL, 0}
};

// ================================================================================
// [6] 初期化とメソッド登録
// ================================================================================

// 登録用テーブル
static const struct nx_method_table {
    const char *name;
    mrb_func_t  func;
    mrb_aspec   aspec;
} input_methods[] = {
    {"set_repeat"      , nx_input_set_repeat         , MRB_ARGS_REQ(2)},
    {"x"               , nx_input_x                  , MRB_ARGS_NONE()},
    {"y"               , nx_input_y                  , MRB_ARGS_NONE()},
    
    // キーボード
    {"key_down?"       , nx_input_key_down           , MRB_ARGS_REQ(1)},
    {"key_push?"       , nx_input_key_push           , MRB_ARGS_REQ(1)},
    {"key_release?"    , nx_input_key_release        , MRB_ARGS_REQ(1)},
    
    // ゲームパッド
    {"pad_down?"       , nx_input_pad_down           , MRB_ARGS_REQ(1)},
    {"pad_push?"       , nx_input_pad_push           , MRB_ARGS_REQ(1)},
    {"pad_release?"    , nx_input_pad_release        , MRB_ARGS_REQ(1)},
    {"pad_lx"          , nx_input_pad_lx             , MRB_ARGS_NONE()},
    {"pad_ly"          , nx_input_pad_ly             , MRB_ARGS_NONE()},
    {"pad_rx"          , nx_input_pad_rx             , MRB_ARGS_NONE()},
    {"pad_ry"          , nx_input_pad_ry             , MRB_ARGS_NONE()},

    // マウス
    {"mouse_enable="   , nx_input_set_mouse_enable   , MRB_ARGS_REQ(1)},
    {"set_mouse_pos"   , nx_input_set_mouse_pos      , MRB_ARGS_REQ(2)},
    {"mouse_x"         , nx_input_mouse_x            , MRB_ARGS_NONE()},
    {"mouse_y"         , nx_input_mouse_y            , MRB_ARGS_NONE()},
    {"mouse_wheel_pos" , nx_input_mouse_wheel_pos    , MRB_ARGS_NONE()},
    {"mouse_wheel_pos=", nx_input_set_mouse_wheel_pos, MRB_ARGS_REQ(1)},
    {"mouse_down?"     , nx_input_mouse_down         , MRB_ARGS_REQ(1)},
    {"mouse_push?"     , nx_input_mouse_push         , MRB_ARGS_REQ(1)},
    {"mouse_release?"  , nx_input_mouse_release      , MRB_ARGS_REQ(1)},
    
    // 終端マーク
    {NULL, NULL, 0} 
};

// Inputメソッドの初期化
void nx_input_init(mrb_state *mrb) {
    // ゲームパッドサブシステムの初期化
    SDL_InitSubSystem(SDL_INIT_GAMEPAD);

    struct RClass *Input = mrb_define_module(mrb, "Input");

    // オートリピートの設定を明示的に 0 (無効) で初期化
    for (int i = 0; i < SDL_SCANCODE_COUNT; i++) {
        input_state.key_wait[i] = 0;
        input_state.key_interval[i] = 0;
        input_state.pad_wait[i] = 0;
        input_state.pad_interval[i] = 0;
    }

    // マウスボタン定数の登録
    mrb_define_const(mrb, Input, "M_LBUTTON", mrb_int_value(mrb, 0));
    mrb_define_const(mrb, Input, "M_RBUTTON", mrb_int_value(mrb, 1));
    mrb_define_const(mrb, Input, "M_MBUTTON", mrb_int_value(mrb, 2));

    // キーコードの登録
    for (int i = 0; key_table[i].name != NULL; i++) {
        mrb_define_const(mrb, Input, key_table[i].name, mrb_int_value(mrb, key_table[i].code));
    }

    // パッド定数の登録
    for (int i = 0; pad_table[i].name != NULL; i++) {
        mrb_define_const(mrb, Input, pad_table[i].name, mrb_int_value(mrb, pad_table[i].code));
    }

    // メソッドの登録
    for (int i = 0; input_methods[i].name; i++) {
        mrb_define_module_function(mrb, Input, input_methods[i].name, input_methods[i].func, input_methods[i].aspec);
    }
}
