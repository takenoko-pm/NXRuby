#include <mruby.h>
#include <mruby/compile.h>
#include <mruby/array.h>
#include <mruby/error.h>
#include <SDL3/SDL.h>
#include "window.h"

// windowモジュールAPIを実装する

// ウィンドウの状態を管理する構造体
typedef struct {
    bool          is_ready;
    SDL_Window   *window;
    SDL_Renderer *renderer;
    // windowの要素
    char          caption[256];
    int           width;
    int           height;
    SDL_Color     bgcolor;
    // FPS制御用(ナノ秒単位)
    int           target_fps;       // 目標FPS（デフォルト60）
    Uint64        next_time;        // 次のフレームが始まるべき理想の時刻
    // FPS計測用(ナノ秒単位)
    Uint64        base_time;        // 計測開始時刻
    int           frame_cnt;        // 1秒間に何回描画したか
    float         real_fps;         // 実際のFPS
} WindowState;

static WindowState window_state = {
    .is_ready    = false,
    .window      = NULL,
    .renderer    = NULL,
    .caption     = "NXRuby",
    .width       = 640, 
    .height      = 480, 
    .bgcolor     = {0, 0, 0, 255},
    .target_fps  = 60,         // デフォルトは60FPS
    .next_time   = 0,
    .base_time   = 0,
    .frame_cnt   = 0,
    .real_fps    = 0.0f
};   // 実体を作っておく

// ループ状態を管理する構造体（mrbgem化を見据えたカプセル化）
typedef struct {
    bool       is_registered;
    mrb_state *mrb;
    mrb_value  block;
} LoopState;

static LoopState *loop_state = NULL;

// 登録処理
// MRB_ARGS_REQ()内の数字は引数の数
void nx_window_init(mrb_state *mrb) {
    struct RClass *Window = mrb_define_module(mrb, "Window");
    
    mrb_define_module_function(mrb, Window, "loop"    , nx_window_loop       , MRB_ARGS_BLOCK());
    mrb_define_module_function(mrb, Window, "close"   , nx_window_close      , MRB_ARGS_NONE());
    mrb_define_module_function(mrb, Window, "caption" ,  nx_window_caption   , MRB_ARGS_NONE());
    mrb_define_module_function(mrb, Window, "caption=", nx_window_set_caption, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, Window, "width"   , nx_window_width      , MRB_ARGS_NONE());
    mrb_define_module_function(mrb, Window, "width="  , nx_window_set_width  , MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, Window, "height"  , nx_window_height     , MRB_ARGS_NONE());
    mrb_define_module_function(mrb, Window, "height=" , nx_window_set_height , MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, Window, "bgcolor" , nx_window_bgcolor    , MRB_ARGS_NONE());
    mrb_define_module_function(mrb, Window, "bgcolor=", nx_window_set_bgcolor, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, Window, "fps"     , nx_window_fps        , MRB_ARGS_NONE());
    mrb_define_module_function(mrb, Window, "fps="    , nx_window_set_fps    , MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, Window, "real_fps", nx_window_real_fps   , MRB_ARGS_NONE());
}

// 初期化処理(初回実行時)
static void nx_window_create(mrb_state *mrb) {
    if (window_state.is_ready) return;              // 初回以外は何もしない

    // SDL, window, rendererの初期化
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        mrb_raisef(mrb, E_RUNTIME_ERROR, "SDL_Init failed: %s", SDL_GetError());
    }
    window_state.window   = SDL_CreateWindow(window_state.caption, window_state.width, window_state.height, SDL_WINDOW_HIDDEN);
    if (!window_state.window) {
        mrb_raisef(mrb, E_RUNTIME_ERROR, "CreateWindow failed: %s", SDL_GetError());
    }
    window_state.renderer = SDL_CreateRenderer(window_state.window, NULL);
    if (!window_state.renderer) {
        SDL_DestroyWindow(window_state.window);
        mrb_raisef(mrb, E_RUNTIME_ERROR, "CreateRenderer failed: %s", SDL_GetError());
    }

    // タイマーの初期化(+ VSyncのオフ明示)
    SDL_SetRenderVSync(window_state.renderer, 0);
    Uint64 now_time = SDL_GetTicksNS();
    window_state.next_time = now_time;
    window_state.base_time = now_time;

    window_state.is_ready = true;
}

mrb_value nx_window_loop(mrb_state *mrb, mrb_value self) {
    // すでにループが登録されている場合は例外を発生させる
    if (loop_state && loop_state->is_registered) {
        // E_RUNTIME_ERROR は Ruby の RuntimeError に相当
        mrb_raise(mrb, E_RUNTIME_ERROR, "Window.loop can only be called once!");
    }
    
    nx_window_create(mrb);              // windowの生成チェック 

    mrb_value block;
    mrb_get_args(mrb, "&", &block);     // Ruby側からブロック（do...end）を受け取る

    if (mrb_nil_p(block)) {
        return mrb_false_value();       // ブロックがなければエラー
    }

    // コンテキストの確保（ループ毎に初期化）
    if (!loop_state) {
        loop_state = mrb_malloc(mrb, sizeof(LoopState));
    }
    loop_state->mrb = mrb;
    loop_state->block = block;
    loop_state->is_registered = true;
    
    mrb_gc_register(mrb, block);        // Rubyのブロックがガベージコレクションで消されるのを防ぐ
    SDL_ShowWindow(window_state.window);

    return mrb_nil_value();             // 正常終了
}

// mrb_protect 経由で呼び出されるブロック実行用ラッパー
static mrb_value nx_window_execute_block(mrb_state *mrb, mrb_value block) {
    return mrb_yield_argv(mrb, block, 0, NULL);
}

// SDL3のコールバック（SDL_AppIterate）から毎フレーム呼ばれる関数
bool nx_window_tick(void) {
    // ブロックがまだ登録されていない場合は、何もしない（エラーにしない）
    if (!loop_state || !loop_state->is_registered) return true;

    // FPS制御
    Uint64 tick_start = SDL_GetTicksNS();
    if (window_state.target_fps > 0) {
        if (tick_start < window_state.next_time) {
#ifdef __EMSCRIPTEN__
            // Webの場合：時間が来るまで「何もせずに」ブラウザへ処理を返す（スキップ）
            return true;
#else
            // PCの場合：時間が来るまでスリープして待つ
            SDL_DelayNS(window_state.next_time - tick_start);
            tick_start = SDL_GetTicksNS(); // スリープから目覚めた後の時間に更新
#endif
        } else {
            // 処理落ちした場合は基準を現在時刻に
            window_state.next_time = tick_start;
        }
        
        Uint64 frame_time = 1000000000ULL / window_state.target_fps;    // 1フレームの時間（1秒 = 10億ナノ秒）
        window_state.next_time += frame_time;                           // 次のフレームの時刻をセット
    }

    // クリアする色を指定してからクリアする
    SDL_SetRenderDrawColor(
        window_state.renderer, 
        window_state.bgcolor.r, 
        window_state.bgcolor.g, 
        window_state.bgcolor.b, 
        window_state.bgcolor.a
    );
    SDL_RenderClear(window_state.renderer);

    // 保護バリア（mrb_protect）を張ってRubyのコードを実行！
    mrb_bool is_exc = false; // エラーが起きたかどうかのフラグ
    mrb_protect(loop_state->mrb, nx_window_execute_block, loop_state->block, &is_exc);

    // Ruby側でエラーが発生した場合は false を返し、アプリを終了させる
    if (is_exc) {
        mrb_print_error(loop_state->mrb);
        return false; 
    }

    // 画面を更新
    SDL_RenderPresent(window_state.renderer);

    // FPS計測
    window_state.frame_cnt++;
    Uint64 tick_end  = SDL_GetTicksNS();
    Uint64 passed_ns = tick_end - window_state.base_time;
    // 1秒(10億ナノ秒)経過したら
    if (tick_end - window_state.base_time >= 1000000000ULL) {
        float passed_sec       = (float)passed_ns / 1000000000.0f;
        window_state.real_fps  = (float)window_state.frame_cnt / passed_sec;
        window_state.frame_cnt = 0;
        window_state.base_time = tick_end;
    }

    return true; // 正常なのでループ継続
}

// アプリ終了時（SDL_AppQuit）に呼ばれる後片付け
void nx_window_cleanup(void) {
    if (loop_state) {
        if (loop_state->is_registered) {
            // 保護を解除して、メモリを安全に解放
            mrb_gc_unregister(loop_state->mrb, loop_state->block);
        }
        mrb_free(loop_state->mrb, loop_state);
        loop_state = NULL;
    }

    if (window_state.renderer) SDL_DestroyRenderer(window_state.renderer);
    if (window_state.window)   SDL_DestroyWindow(window_state.window);
}

// ユーザーが記述する終了処理
mrb_value nx_window_close(mrb_state *mrb, mrb_value self) {
    // SDLに「アプリを終了して(QUIT)」というイベントを自作して送信する
    SDL_Event event;
    SDL_zero(event);        // 構造体をゼロクリア（安全のため）
    event.type = SDL_EVENT_QUIT;
    
    SDL_PushEvent(&event);  // イベントキューに押し込む

    return mrb_nil_value();
}

mrb_value nx_window_caption(mrb_state *mrb, mrb_value self) {
    return mrb_str_new_cstr(mrb, window_state.caption);
}

mrb_value nx_window_set_caption(mrb_state *mrb, mrb_value self) {
    char *str;
    mrb_get_args(mrb, "z", &str);

    // バッファオーバーランを防ぎつつ構造体の配列にコピー
    snprintf(window_state.caption, sizeof(window_state.caption), "%s", str);

    // ウィンドウが生成済みならタイトルを更新する
    if (window_state.window) {
        SDL_SetWindowTitle(window_state.window, window_state.caption);
    }

    return mrb_str_new_cstr(mrb, window_state.caption);
}

mrb_value nx_window_width(mrb_state *mrb, mrb_value self) {
    return mrb_int_value(mrb, window_state.width);
}

mrb_value nx_window_set_width(mrb_state *mrb, mrb_value self) {
    mrb_int w;
    mrb_get_args(mrb, "i", &w);
    if (w < 1) w = 1; // 最小サイズ制限

    window_state.width = (int)w;

    // もし既にウィンドウが作られていれば、実体をリサイズする
    if (window_state.window) {
        SDL_SetWindowSize(window_state.window, window_state.width, window_state.height);
    }
    return mrb_int_value(mrb, w);
}

mrb_value nx_window_height(mrb_state *mrb, mrb_value self) {
    return mrb_int_value(mrb, window_state.height);
}

mrb_value nx_window_set_height(mrb_state *mrb, mrb_value self) {
    mrb_int h;
    mrb_get_args(mrb, "i", &h);
    if (h < 1) h = 1; // 最小サイズ制限

    window_state.height = (int)h;

    // もし既にウィンドウが作られていれば、実体をリサイズする
    if (window_state.window) {
        SDL_SetWindowSize(window_state.window, window_state.width, window_state.height);
    }
    return mrb_int_value(mrb, h);
}

mrb_value nx_window_bgcolor(mrb_state *mrb, mrb_value self) {
    // 1. Rubyに渡す配列を用意
    mrb_value arr[4];
    
    // 2. 構造体の数値を配列に代入
    arr[0] = mrb_int_value(mrb, window_state.bgcolor.r);
    arr[1] = mrb_int_value(mrb, window_state.bgcolor.g);
    arr[2] = mrb_int_value(mrb, window_state.bgcolor.b);
    arr[3] = mrb_int_value(mrb, window_state.bgcolor.a);

    // 3. Rubyの配列として返す
    return mrb_ary_new_from_values(mrb, 4, arr);
}

mrb_value nx_window_set_bgcolor(mrb_state *mrb, mrb_value self) {
    mrb_value arr;
    mrb_get_args(mrb, "A", &arr);

    // 配列の要素数が足りているかチェック
    if (RARRAY_LEN(arr) < 3) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "Array must have at least 3 elements (R, G, B)");
    }

    mrb_int r = mrb_as_int(mrb, mrb_ary_entry(arr, 0));
    mrb_int g = mrb_as_int(mrb, mrb_ary_entry(arr, 1));
    mrb_int b = mrb_as_int(mrb, mrb_ary_entry(arr, 2));

    // 0〜255 の範囲にクランプ（制限）する
    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;

    // Uint8 にキャストして構造体へ
    window_state.bgcolor.r = (Uint8)r;
    window_state.bgcolor.g = (Uint8)g;
    window_state.bgcolor.b = (Uint8)b;

    // A（アルファ）の処理
    if (RARRAY_LEN(arr) >= 4) {
        mrb_int a = mrb_as_int(mrb, mrb_ary_entry(arr, 3));
        if (a < 0) a = 0; else if (a > 255) a = 255;
        window_state.bgcolor.a = (Uint8)a;
    } else {
        window_state.bgcolor.a = 255; // 省略されたら不透明(255)
    }

    return arr;
}

mrb_value nx_window_fps(mrb_state *mrb, mrb_value self) {
    return mrb_int_value(mrb, window_state.target_fps);
}

mrb_value nx_window_real_fps(mrb_state *mrb, mrb_value self) {
    return mrb_float_value(mrb, window_state.real_fps);
}

mrb_value nx_window_set_fps(mrb_state *mrb, mrb_value self) {
    mrb_int fps;
    mrb_get_args(mrb, "i", &fps);
    if (fps < 0) fps = 0; // 0は無制限として扱う
    window_state.target_fps = (int)fps;
    return mrb_int_value(mrb, fps);
}
