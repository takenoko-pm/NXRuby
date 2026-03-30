// main.c
//
// main関数ではなくSDL3のCALLBACKSで処理をする
// web(emscripten)含めたマルチプラットホームで安定に動作させるため
//
// mrbgem化させる際、この部分をどうするかは未定

// ログ規約: SDL_Log*** で出す + [NXRuby] でタグ付け

#define SDL_MAIN_USE_CALLBACKS 1
#include <mruby.h>
#include <mruby/compile.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "window.h"
#include "input.h"
#include "image.h"
#include "font.h"
#include "sound.h"
#include "sprite.h"

#ifdef _WIN32
    #include <direct.h>
    #define CHDIR _chdir
#else
    #include <unistd.h>
    #define CHDIR chdir
#endif

// mrb実体の生成
// これをすることで、C言語側からmrubyを管理できる
static mrb_state *mrb = NULL;

// --- 初期化処理 ---
// アプリ起動時に1回だけ呼ばれる
SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
    // 正常にmrb構造体が生成できているか確認
    mrb = mrb_open();
    if (!mrb) return SDL_APP_FAILURE;

    // initを呼び出す
    nx_window_init(mrb);
    nx_input_init(mrb);
    nx_image_init(mrb);
    nx_font_init(mrb);
    nx_sound_init(mrb);
    nx_sprite_init(mrb);

    // 起動した瞬間に、すべての基準となるディレクトリを「アプリがある場所」に強制変更する
    const char *base_path = SDL_GetBasePath();
    if (base_path) {
        CHDIR(base_path);
    }

    // コマンドライン引数からスクリプト名を取得
    const char *script_name = (argc > 1) ? argv[1] : "src/main.rb";

    // main.rb をロードする
    FILE *fp = fopen(script_name, "r");
    if (fp) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[NXRuby] Loading %s...", script_name);
        mrb_load_file(mrb, fp);
        fclose(fp);
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[NXRuby] %s not found!", script_name);
        return SDL_APP_FAILURE;
    }

    // 構文エラーなどがあればアプリ終了
    if (mrb->exc) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "[NXRuby] Ruby script execution failed.");
        mrb_print_error(mrb);
        return SDL_APP_FAILURE;
    }

    // すべて成功していたらループ処理へ
    return SDL_APP_CONTINUE;
}

// --- イベント処理 ---
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    // ウィンドウの論理スケールに合わせてイベント内の座標を変換
    nx_window_convert_event(event);
    // 入力イベントを Input モジュールへ流す
    nx_input_handle_event(event);

    // 終了ボタンが押された場合
    if (event->type == SDL_EVENT_QUIT) {
        // SDL_AppQuitへ進む（正常終了）
        return SDL_APP_SUCCESS;
    }
    return SDL_APP_CONTINUE;
}

// --- ループ処理 ---
// whileに相当する
SDL_AppResult SDL_AppIterate(void *appstate) {
    // window.c の tick 関数に処理を丸投げ
    if (!nx_window_tick()) {
        // エラーが起きたら終了
        return SDL_APP_FAILURE;
    }
    return SDL_APP_CONTINUE;
}

// --- 終了処理 ---
// SDL側は自動で片付くので、mruby側を片付ける（SDL_Quitは必要ない）
void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    // ここにcleanupを追加
    nx_window_cleanup();
    nx_sound_cleanup();
    TTF_Quit();
    
    // mrubyの終了（自動的に gem_final が呼ばれ、nx_window_cleanup も実行される）
    if (mrb) {
        mrb_close(mrb);
        mrb = NULL;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "[NXRuby] NXRuby App Quit.");
}
