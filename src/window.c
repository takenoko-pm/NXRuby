#include <mruby.h>
#include <mruby/compile.h>
#include <SDL3/SDL.h>
#include "window.h"

// windowモジュールAPIを実装する

// ウィンドウの状態を管理する構造体
typedef struct {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    int width;
    int height;
    bool is_ready;
} WindowState;

static WindowState window_state = {NULL, NULL, 640, 480, false};   // 実体を作っておく

// ループ状態を管理する構造体（mrbgem化を見据えたカプセル化）
typedef struct {
    mrb_state *mrb;
    mrb_value block;
    bool is_registered;
} LoopState;

static LoopState *loop_state = NULL;

// 登録処理
// MRB_ARGS_REQ()内の数字は引数の数
void nx_window_init(mrb_state *mrb) {
    struct RClass *Window = mrb_define_module(mrb, "Window");
    
    mrb_define_module_function(mrb, Window, "loop" , nx_window_loop , MRB_ARGS_BLOCK());
    mrb_define_module_function(mrb, Window, "color", nx_window_color, MRB_ARGS_REQ(4));
}

// 初期化処理(初回実行時)
static void nx_window_create(void) {
    if (window_state.is_ready) return;           // 初回以外は何もしない
    SDL_Init(SDL_INIT_VIDEO);           // SDL, window, rendererの初期化
    window_state.window   = SDL_CreateWindow("NXRuby", 640, 480, SDL_WINDOW_HIDDEN);
    window_state.renderer = SDL_CreateRenderer(window_state.window, NULL);
    window_state.is_ready = true;
}

mrb_value nx_window_loop(mrb_state *mrb, mrb_value self) {
    nx_window_create();                 // windowの生成チェック 

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

    return mrb_true_value();            // 正常終了
}

// SDL3のコールバック（SDL_AppIterate）から毎フレーム呼ばれる関数
bool nx_window_tick(void) {
    // ブロックがまだ登録されていない場合は、何もしない（エラーにしない）
    if (!loop_state || !loop_state->is_registered) {
        return true; 
    }

    SDL_RenderClear(window_state.renderer);

    // 保護したブロックを実行（ゲームロジック）
    mrb_yield(loop_state->mrb, loop_state->block, mrb_nil_value());

    // Ruby側でエラーが発生した場合は false を返し、アプリを終了させる
    if (loop_state->mrb->exc) {
        mrb_print_error(loop_state->mrb);
        return false; 
    }

    SDL_RenderPresent(window_state.renderer);
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
    
    // SDL_Init を呼んだので QuitSubSystem で閉じる
    if (window_state.is_ready) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        window_state.is_ready = false;
    }
}

mrb_value nx_window_color(mrb_state *mrb, mrb_value self) {
    nx_window_create();                         // windowの生成チェック 
    mrb_int r, g, b, a;
    mrb_get_args(mrb, "iiii", &r, &g, &b, &a);  // RGBAを受け取る
    SDL_SetRenderDrawColor(window_state.renderer, (Uint8)r, (Uint8)g, (Uint8)b, (Uint8)a);
    return mrb_true_value();
}
