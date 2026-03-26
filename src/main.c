#define SDL_MAIN_USE_CALLBACKS 1
#include <mruby.h>
#include <mruby/compile.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "window.h"
#include "input.h"

static mrb_state *mrb = NULL;

// 1. アプリ起動時に1回だけ呼ばれる（初期化とスクリプト読み込み）
SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
    mrb = mrb_open();
    if (!mrb) return SDL_APP_FAILURE;

    // APIの登録
    nx_window_init(mrb);
    nx_input_init(mrb);

    // app.rb をロードする
    // この中で Window.loop が評価され、ブロックが記憶される
    FILE *fp = fopen("app.rb", "r");
    if (fp) {
        SDL_Log("NXRuby: Loading app.rb...");
        mrb_load_file(mrb, fp);
        fclose(fp);
    } else {
        SDL_Log("NXRuby Error: app.rb not found!");
        return SDL_APP_FAILURE;
    }

    // 構文エラーなどがあればアプリ終了
    if (mrb->exc) {
        SDL_Log("NXRuby Error: Ruby script execution failed.");
        mrb_print_error(mrb);
        return SDL_APP_FAILURE;
    }

    return SDL_APP_CONTINUE; // 成功。ループへ移行
}

// 2. ウィンドウを閉じるボタンなどが押された時のイベント処理
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS; // 正常終了を要求（SDL_AppQuitへ進む）
    }
    return SDL_APP_CONTINUE;
}

// 3. 毎フレーム（秒間60回など）呼ばれるループ処理
SDL_AppResult SDL_AppIterate(void *appstate) {
    // window.c の tick 関数に処理を丸投げ
    if (!nx_window_tick()) {
        return SDL_APP_FAILURE; // エラーが起きたら終了
    }
    return SDL_APP_CONTINUE;
}

// 4. アプリ終了時に呼ばれる後片付け
void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    // 各モジュールの片付け
    nx_window_cleanup();
    
    // mrubyの終了
    if (mrb) {
        mrb_close(mrb);
        mrb = NULL;
    }

    SDL_Log("NXRuby App Quit.");
}
