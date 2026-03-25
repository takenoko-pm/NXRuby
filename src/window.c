#include <mruby.h>
#include <mruby/compile.h>
#include <mruby/array.h>
#include <mruby/error.h>
#include <SDL3/SDL.h>
#include <stdlib.h>
#include <math.h>
#include "window.h"

// windowモジュールAPIを実装する

// 描画コマンドキューの定義
typedef enum {
    CMD_PIXEL,
    CMD_LINE,
    CMD_RECT,
    CMD_RECT_FILL,
    CMD_CIRCLE,
    CMD_CIRCLE_FILL
} CmdType;

typedef struct {
    CmdType   type;
    SDL_Color color;
    float     z;              // 描画順（小さいほど奥、大きいほど手前）
    int       order;
    union {
        struct { float  x,  y; } pixel;
        struct { float x1, y1, x2, y2; } line;
        struct { float  x,  y,  w,  h; } rect;
        struct { float cx, cy, cr; } circle;
    } data;
} DrawCmd;

// 描画キュー
#define MAX_DRAW_CMDS 65536
static DrawCmd draw_queue[MAX_DRAW_CMDS];
static int draw_cmd_count = 0;

// ウィンドウの状態を管理する構造体
typedef struct {
    bool          is_ready;
    SDL_Window   *window;
    SDL_Renderer *renderer;
    // windowの要素
    char          caption[256];
    int           width;
    int           height;
    int           x;
    int           y;
    bool          is_fullscreen;
    float         scale;
    float         ox;
    float         oy;
    SDL_Color     bgcolor;
    // FPS関連
    Uint64        last_ns;          // 前フレームの開始時刻
    Uint64        lag_ns;           // 固定FPS更新のための遅延時間（ナノ秒単位）
    int           target_fps;       // 目標FPS
    int           logic_fps;        // 実際の処理FPS
    int           logic_count;      // 処理のカウント
    int           render_fps;       // 実際の描画FPS
    int           render_count;     // 描画のカウント
    Uint64        fps_timer;        // 計測用のタイマー時刻
} WindowState;

static WindowState window_state = {
    .is_ready      = false,
    .window        = NULL,
    .renderer      = NULL,
    .caption       = "NXRuby",
    .width         = 640, 
    .height        = 480, 
    .x             = SDL_WINDOWPOS_CENTERED,
    .y             = SDL_WINDOWPOS_CENTERED,
    .is_fullscreen = false,
    .scale         = 1.0f,
    .ox            = 0.0f,
    .oy            = 0.0f,
    .bgcolor       = {0, 0, 0, 255},
    .last_ns       = 0,
    .lag_ns        = 0,
    .target_fps    = 60,
    .logic_fps     = 0,
    .logic_count   = 0,
    .render_fps    = 0,
    .render_count  = 0,
    .fps_timer     = 0
};   // 実体を作っておく

// ループ状態を管理する構造体（mrbgem化を見据えたカプセル化）
typedef struct {
    bool       is_registered;
    mrb_state *mrb;
    mrb_value  block;
} LoopState;

static LoopState *loop_state = NULL;

// 登録用テーブル
static const struct nx_method_table {
    const char *name;
    mrb_func_t  func;
    mrb_aspec   aspec;
} window_methods[] = {
    {"loop"            , nx_window_loop            , MRB_ARGS_BLOCK()},
    {"close"           , nx_window_close           , MRB_ARGS_NONE()},
    {"logic_fps"       , nx_window_logic_fps       , MRB_ARGS_NONE()},
    {"render_fps"      , nx_window_render_fps      , MRB_ARGS_NONE()},
    {"fps"             , nx_window_fps             , MRB_ARGS_NONE()},
    {"fps="            , nx_window_set_fps         , MRB_ARGS_REQ(1)},
    {"caption"         ,  nx_window_caption        , MRB_ARGS_NONE()},
    {"caption="        , nx_window_set_caption     , MRB_ARGS_REQ(1)},
    {"width"           , nx_window_width           , MRB_ARGS_NONE()},
    {"width="          , nx_window_set_width       , MRB_ARGS_REQ(1)},
    {"height"          , nx_window_height          , MRB_ARGS_NONE()},
    {"height="         , nx_window_set_height      , MRB_ARGS_REQ(1)},
    {"x"               , nx_window_x               , MRB_ARGS_NONE()},
    {"x="              , nx_window_set_x           , MRB_ARGS_REQ(1)},
    {"y"               , nx_window_y               , MRB_ARGS_NONE()},
    {"y="              , nx_window_set_y           , MRB_ARGS_REQ(1)},
    {"full_screen?"    , nx_window_full_screen     , MRB_ARGS_NONE()},
    {"full_screen="    , nx_window_set_full_screen , MRB_ARGS_REQ(1)},
    {"windowed?"       , nx_window_windowed        , MRB_ARGS_NONE()},
    {"windowed="       , nx_window_set_windowed    , MRB_ARGS_REQ(1)},
    {"running_time"    , nx_window_running_time    , MRB_ARGS_NONE()},
    {"scale"           , nx_window_scale           , MRB_ARGS_NONE()},
    {"scale="          , nx_window_set_scale       , MRB_ARGS_REQ(1)},
    {"active?"         , nx_window_active          , MRB_ARGS_NONE()},
    {"ox"              , nx_window_ox              , MRB_ARGS_NONE()},
    {"ox="             , nx_window_set_ox          , MRB_ARGS_REQ(1)},
    {"oy"              , nx_window_oy              , MRB_ARGS_NONE()},
    {"oy="             , nx_window_set_oy          , MRB_ARGS_REQ(1)},
    {"bgcolor"         , nx_window_bgcolor         , MRB_ARGS_NONE()},
    {"bgcolor="        , nx_window_set_bgcolor     , MRB_ARGS_REQ(1)},
    {"draw_pixel"      , nx_window_draw_pixel      , MRB_ARGS_REQ(3) | MRB_ARGS_OPT(1)},
    {"draw_line"       , nx_window_draw_line       , MRB_ARGS_REQ(5) | MRB_ARGS_OPT(1)},
    {"draw_rect"       , nx_window_draw_rect       , MRB_ARGS_REQ(5) | MRB_ARGS_OPT(1)},
    {"draw_rect_fill"  , nx_window_draw_rect_fill  , MRB_ARGS_REQ(5) | MRB_ARGS_OPT(1)},
    {"draw_circle"     , nx_window_draw_circle     , MRB_ARGS_REQ(4) | MRB_ARGS_OPT(1)},
    {"draw_circle_fill", nx_window_draw_circle_fill, MRB_ARGS_REQ(4) | MRB_ARGS_OPT(1)},
    {NULL, NULL, 0} 
};

// 登録処理
// MRB_ARGS_REQ()内の数字は引数の数
void nx_window_init(mrb_state *mrb) {
    struct RClass *Window = mrb_define_module(mrb, "Window");

    for (int i = 0; window_methods[i].name; i++) {
        mrb_define_module_function(mrb, Window, window_methods[i].name, window_methods[i].func, window_methods[i].aspec);
    }
}

// 初期化処理(初回実行時)
static void nx_window_create(mrb_state *mrb) {
    if (window_state.is_ready) return;              // 初回以外は何もしない

    // SDLの初期化
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        mrb_raisef(mrb, E_RUNTIME_ERROR, "SDL_Init failed: %s", SDL_GetError());
    }

    // 生成時にフルスクリーン状態を反映
    SDL_WindowFlags flags = SDL_WINDOW_HIDDEN;
    if (window_state.is_fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN;
    }

    // Windowの生成
    window_state.window   = SDL_CreateWindow(window_state.caption, window_state.width, window_state.height, flags);
    if (!window_state.window) {
        mrb_raisef(mrb, E_RUNTIME_ERROR, "CreateWindow failed: %s", SDL_GetError());
    }

    // 作成後に座標を適用（SDL_WINDOWPOS_CENTERED もここで処理される）
    SDL_SetWindowPosition(window_state.window, window_state.x, window_state.y);

    // Rendererの生成
    window_state.renderer = SDL_CreateRenderer(window_state.window, NULL);
    if (!window_state.renderer) {
        SDL_DestroyWindow(window_state.window);
        mrb_raisef(mrb, E_RUNTIME_ERROR, "CreateRenderer failed: %s", SDL_GetError());
    }
    SDL_SetRenderLogicalPresentation(window_state.renderer, window_state.width, window_state.height, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderDrawBlendMode(window_state.renderer, SDL_BLENDMODE_BLEND);
    
    SDL_SetRenderVSync(window_state.renderer, 1);   // VSyncを常時ON
    window_state.last_ns = SDL_GetTicksNS();        // タイマーの初期化
    window_state.fps_timer = window_state.last_ns;

    window_state.is_ready = true;
}

static void nx_window_push_cmd(DrawCmd cmd) {
    if (draw_cmd_count < MAX_DRAW_CMDS) {
        cmd.order = draw_cmd_count;                 // キューに入った順番を記録
        draw_queue[draw_cmd_count++] = cmd;
    } else {
        static bool warned = false;
        if (!warned) {
            SDL_Log("NXRuby Warning: Draw command queue overflow! Max %d", MAX_DRAW_CMDS);
            warned = true;
        }
    }
}

static void nx_window_render_circle(SDL_Renderer *renderer, float cx, float cy, float cr) {
    int x = (int)cr, y = 0, err = 0;
    while (x >= y) {
        SDL_RenderPoint(renderer, cx + x, cy + y);
        if (x != 0) SDL_RenderPoint(renderer, cx - x, cy + y);
        if (y != 0) SDL_RenderPoint(renderer, cx + x, cy - y);
        if (x != 0 && y != 0) SDL_RenderPoint(renderer, cx - x, cy - y);
        if (x != y) {
            SDL_RenderPoint(renderer, cx + y, cy + x);
            if (y != 0) SDL_RenderPoint(renderer, cx - y, cy + x);
            if (x != 0) SDL_RenderPoint(renderer, cx + y, cy - x);
            if (x != 0 && y != 0) SDL_RenderPoint(renderer, cx - y, cy - x);
        }
        y += 1;
        if (err <= 0) err += 2 * y + 1;
        else { x -= 1; err += 2 * (y - x) + 1; }
    }
}

static void nx_window_render_circle_fill(SDL_Renderer *renderer, float cx, float cy, float cr) {
    int x = (int)cr, y = 0, err = 0, last_x = -1;
    while (x >= y) {
        SDL_RenderLine(renderer, cx - x, cy + y, cx + x, cy + y);
        if (y != 0) SDL_RenderLine(renderer, cx - x, cy - y, cx + x, cy - y);
        
        if (x != last_x && x != y) {
            SDL_RenderLine(renderer, cx - y, cy + x, cx + y, cy + x);
            if (x != 0) SDL_RenderLine(renderer, cx - y, cy - x, cx + y, cy - x);
            last_x = x;
        }
        y += 1;
        if (err <= 0) err += 2 * y + 1;
        else { x -= 1; err += 2 * (y - x) + 1; }
    }
}

// Zが小さい（奥）から大きい（手前）へソート（Z座標が同じなら、Ruby側で呼ばれた順番を維持）
static int nx_window_sort_z(const void *a, const void *b) {
    const DrawCmd *cmdA = (const DrawCmd*)a;
    const DrawCmd *cmdB = (const DrawCmd*)b;
    
    if (cmdA->z > cmdB->z) return 1;
    if (cmdA->z < cmdB->z) return -1;
    
    return cmdA->order - cmdB->order;
}

static void nx_window_draw_queue(void) {
    // Zソートの実行
    qsort(draw_queue, draw_cmd_count, sizeof(DrawCmd), nx_window_sort_z);

    // 順番にSDLへ描画
    for (int i = 0; i < draw_cmd_count; i++) {
        DrawCmd *cmd = &draw_queue[i];
        SDL_SetRenderDrawColor(window_state.renderer, cmd->color.r, cmd->color.g, cmd->color.b, cmd->color.a);

        switch (cmd->type) {
            case CMD_PIXEL: {
                SDL_RenderPoint(window_state.renderer, cmd->data.pixel.x, cmd->data.pixel.y);
                break;
            }
            case CMD_LINE: {
                SDL_RenderLine(window_state.renderer, cmd->data.line.x1, cmd->data.line.y1, cmd->data.line.x2, cmd->data.line.y2);
                break;
            }
            case CMD_RECT: {
                SDL_FRect rect = {cmd->data.rect.x, cmd->data.rect.y, cmd->data.rect.w, cmd->data.rect.h};
                SDL_RenderRect(window_state.renderer, &rect);
                break;
            }
            case CMD_RECT_FILL: {
                SDL_FRect rect = {cmd->data.rect.x, cmd->data.rect.y, cmd->data.rect.w, cmd->data.rect.h};
                SDL_RenderFillRect(window_state.renderer, &rect);
                break;
            }
            case CMD_CIRCLE: {
                nx_window_render_circle(window_state.renderer, cmd->data.circle.cx, cmd->data.circle.cy, cmd->data.circle.cr);
                break;
            }
            case CMD_CIRCLE_FILL: {
                nx_window_render_circle_fill(window_state.renderer, cmd->data.circle.cx, cmd->data.circle.cy, cmd->data.circle.cr);
                break;
            }
            default: break;
        }
    }
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
    if (mrb_nil_p(block)) {             // ブロックがなければエラー
        mrb_raise(mrb, E_ARGUMENT_ERROR, "Window.loop requires a block (do...end)");
    }

    // コンテキストの確保（ループ毎に初期化）
    if (!loop_state) loop_state = mrb_malloc(mrb, sizeof(LoopState));
    loop_state->mrb             = mrb;
    loop_state->block           = block;
    loop_state->is_registered   = true;
    
    mrb_gc_register(mrb, block);        // Rubyのブロックがガベージコレクションで消されるのを防ぐ
    SDL_ShowWindow(window_state.window);

    return mrb_nil_value();             // 正常終了
}

// mrb_protect用のラッパー関数
static mrb_value nx_window_yield_protected(mrb_state *mrb, mrb_value block) {
    return mrb_yield_argv(mrb, block, 0, NULL);
}

// Rubyブロック実行の本体
static bool nx_window_call_ruby_block(void) {
    window_state.logic_count++; // Rubyの処理が回った回数を数える
    draw_cmd_count = 0;         // 実行前にキューを空にする
    mrb_bool is_exc = false;    // エラーが起きたかどうかのフラグ

    // 保護バリア（mrb_protect）を張ってRubyのコード（do...end）を実行
    int arena_idx = mrb_gc_arena_save(loop_state->mrb);         // 今のアリーナの状態を保存
    mrb_protect(loop_state->mrb, nx_window_yield_protected, loop_state->block, &is_exc);
    mrb_gc_arena_restore(loop_state->mrb, arena_idx);           // アリーナを復元（ゴミを解放）する

    if (is_exc) {
        mrb_print_error(loop_state->mrb);
        return false;
    }

    return true;
}

// FPS計測の内部関数
static void nx_window_update_fps(Uint64 now_ns) {
    if (now_ns - window_state.fps_timer >= 1000000000ULL) {
        window_state.logic_fps    = window_state.logic_count;
        window_state.render_fps   = window_state.render_count;
        window_state.logic_count  = 0;
        window_state.render_count = 0;
        window_state.fps_timer    = now_ns;
    }
}

// SDL3のコールバック（SDL_AppIterate）から毎フレーム呼ばれる関数
bool nx_window_tick(void) {
    // Window.loop が呼ばれていなければアプリを終了させる
    if (!loop_state || !loop_state->is_registered) {
        SDL_Log("NXRuby Error: Window.loop requires a block.");
        return false;
    }

    Uint64 now_ns = SDL_GetTicksNS();
    nx_window_update_fps(now_ns);

    if (window_state.target_fps > 0) {
        // 通常の固定FPSモード
        Uint64 target_ns     = 1000000000ULL / window_state.target_fps;
        Uint64 frame_time    = now_ns - window_state.last_ns;
        window_state.last_ns = now_ns;

        // ラグの上限キャップ（処理落ち時の暴走ストッパー）
        if (frame_time > 100000000ULL) frame_time = 100000000ULL; 

        window_state.lag_ns += frame_time;

        while (window_state.lag_ns >= target_ns) {
            if (!nx_window_call_ruby_block()) return false;
            window_state.lag_ns -= target_ns;
        }
    } else {
        // fps = 0 (無制限モード) の場合は、毎回必ず1回だけ処理を回す
        window_state.last_ns = now_ns;
        window_state.lag_ns = 0;
        if (!nx_window_call_ruby_block()) return false;
    }
    
    SDL_SetRenderDrawColor(window_state.renderer, window_state.bgcolor.r, window_state.bgcolor.g, window_state.bgcolor.b, 255);
    SDL_RenderClear(window_state.renderer);     // 画面のクリア
    nx_window_draw_queue();                     // キューに溜まった絵をZソートして一気に描画    
    SDL_RenderPresent(window_state.renderer);   // 画面の更新
    window_state.render_count++;

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
    window_state.is_ready = false;
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

mrb_value nx_window_logic_fps(mrb_state *mrb, mrb_value self) {
    return mrb_int_value(mrb, window_state.logic_fps);
}

mrb_value nx_window_render_fps(mrb_state *mrb, mrb_value self) {
    return mrb_int_value(mrb, window_state.render_fps);
}

mrb_value nx_window_fps(mrb_state *mrb, mrb_value self) {
    return mrb_int_value(mrb, window_state.target_fps);
}

mrb_value nx_window_set_fps(mrb_state *mrb, mrb_value self) {
    mrb_int fps;
    mrb_get_args(mrb, "i", &fps);
    if (fps < 0) fps = 0; // マイナスは0（無制限）として扱う
    
    window_state.target_fps = (int)fps;
    return mrb_int_value(mrb, fps);
}

static SDL_Color nx_window_parse_color(mrb_state *mrb, mrb_value color){
    // 配列であるかチェック
    if (!mrb_array_p(color)) {
        mrb_raise(mrb, E_TYPE_ERROR, "Color must be an Array [R, G, B, (A)]");
    }
    // 配列の要素数が足りているかチェック
    mrb_int len = RARRAY_LEN(color);
    if (len < 3) {
        mrb_raise(mrb, E_ARGUMENT_ERROR, "Color must have at least 3 elements (R, G, B)");
    }

    // 各要素を取り出して 0-255 にクランプ
    mrb_int r = mrb_as_int(mrb, mrb_ary_entry(color, 0));
    mrb_int g = mrb_as_int(mrb, mrb_ary_entry(color, 1));
    mrb_int b = mrb_as_int(mrb, mrb_ary_entry(color, 2));
    mrb_int a = (len >= 4) ? mrb_as_int(mrb, mrb_ary_entry(color, 3)) : 255;

    // クランプ処理
    #define CLAMP_255(v) ((Uint8)((v) < 0 ? 0 : ((v) > 255 ? 255 : (v))))   // ヘルパーマクロ
    SDL_Color sdl_color;
    sdl_color.r = CLAMP_255(r);
    sdl_color.g = CLAMP_255(g);
    sdl_color.b = CLAMP_255(b);
    sdl_color.a = CLAMP_255(a);

    return sdl_color;
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
    if (window_state.window) SDL_SetWindowTitle(window_state.window, window_state.caption);

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
        SDL_SetWindowSize(window_state.window, 
            (int)(window_state.width * window_state.scale), 
            (int)(window_state.height * window_state.scale));
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
        SDL_SetWindowSize(window_state.window, 
            (int)(window_state.width * window_state.scale), 
            (int)(window_state.height * window_state.scale));
    }
    return mrb_int_value(mrb, h);
}

mrb_value nx_window_x(mrb_state *mrb, mrb_value self) {
    if (window_state.window) {
        SDL_GetWindowPosition(window_state.window, &window_state.x, &window_state.y);
    }
    return mrb_int_value(mrb, window_state.x);
}

mrb_value nx_window_set_x(mrb_state *mrb, mrb_value self) {
    mrb_int x;
    mrb_get_args(mrb, "i", &x);
    window_state.x = (int)x;
    if (window_state.window) {
        SDL_SetWindowPosition(window_state.window, window_state.x, window_state.y);
    }
    return mrb_int_value(mrb, x);
}

mrb_value nx_window_y(mrb_state *mrb, mrb_value self) {
    if (window_state.window) {
        SDL_GetWindowPosition(window_state.window, &window_state.x, &window_state.y);
    }
    return mrb_int_value(mrb, window_state.y);
}

mrb_value nx_window_set_y(mrb_state *mrb, mrb_value self) {
    mrb_int y;
    mrb_get_args(mrb, "i", &y);
    window_state.y = (int)y;
    if (window_state.window) {
        SDL_SetWindowPosition(window_state.window, window_state.x, window_state.y);
    }
    return mrb_int_value(mrb, y);
}

mrb_value nx_window_full_screen(mrb_state *mrb, mrb_value self) {
    return mrb_bool_value(window_state.is_fullscreen);
}

mrb_value nx_window_set_full_screen(mrb_state *mrb, mrb_value self) {
    mrb_bool b;
    mrb_get_args(mrb, "b", &b);
    window_state.is_fullscreen = b;
    if (window_state.window) {
        SDL_SetWindowFullscreen(window_state.window, b);
    }
    return mrb_bool_value(b);
}

mrb_value nx_window_windowed(mrb_state *mrb, mrb_value self) {
    return mrb_bool_value(!window_state.is_fullscreen);
}

mrb_value nx_window_set_windowed(mrb_state *mrb, mrb_value self) {
    mrb_bool b;
    mrb_get_args(mrb, "b", &b);
    window_state.is_fullscreen = !b;
    if (window_state.window) {
        SDL_SetWindowFullscreen(window_state.window, !b);
    }
    return mrb_bool_value(b);
}

mrb_value nx_window_running_time(mrb_state *mrb, mrb_value self) {
    return mrb_int_value(mrb, SDL_GetTicks());
}

mrb_value nx_window_scale(mrb_state *mrb, mrb_value self) {
    return mrb_float_value(mrb, window_state.scale);
}

mrb_value nx_window_set_scale(mrb_state *mrb, mrb_value self) {
    mrb_float s;
    mrb_get_args(mrb, "f", &s);
    if (s < 0.1f) s = 0.1f; // 極端に小さくならないようにガード

    window_state.scale = (float)s;

    // ウィンドウの実体サイズを論理サイズ×スケールに変更する
    if (window_state.window) {
        SDL_SetWindowSize(window_state.window, (int)(window_state.width * window_state.scale), (int)(window_state.height * window_state.scale));
    }
    return mrb_float_value(mrb, s);
}

mrb_value nx_window_active(mrb_state *mrb, mrb_value self) {
    if (!window_state.window) return mrb_false_value();
    
    // ウィンドウが現在キーボード入力を受け付けているか（最前面か）を判定
    SDL_WindowFlags flags = SDL_GetWindowFlags(window_state.window);
    return mrb_bool_value((flags & SDL_WINDOW_INPUT_FOCUS) != 0);
}

mrb_value nx_window_ox(mrb_state *mrb, mrb_value self) {
    return mrb_float_value(mrb, window_state.ox);
}

mrb_value nx_window_set_ox(mrb_state *mrb, mrb_value self) {
    mrb_float ox;
    mrb_get_args(mrb, "f", &ox);
    window_state.ox = (float)ox;
    return mrb_float_value(mrb, ox);
}

mrb_value nx_window_oy(mrb_state *mrb, mrb_value self) {
    return mrb_float_value(mrb, window_state.oy);
}

mrb_value nx_window_set_oy(mrb_state *mrb, mrb_value self) {
    mrb_float oy;
    mrb_get_args(mrb, "f", &oy);
    window_state.oy = (float)oy;
    return mrb_float_value(mrb, oy);
}

mrb_value nx_window_bgcolor(mrb_state *mrb, mrb_value self) {
    mrb_value color[4];
    
    // 構造体の数値を配列に代入
    color[0] = mrb_int_value(mrb, window_state.bgcolor.r);
    color[1] = mrb_int_value(mrb, window_state.bgcolor.g);
    color[2] = mrb_int_value(mrb, window_state.bgcolor.b);
    color[3] = mrb_int_value(mrb, window_state.bgcolor.a);

    // Rubyの配列として返す
    return mrb_ary_new_from_values(mrb, 4, color);
}

mrb_value nx_window_set_bgcolor(mrb_state *mrb, mrb_value self) {
    mrb_value color;
    mrb_get_args(mrb, "A", &color);

    // color配列の処理
    window_state.bgcolor   = nx_window_parse_color(mrb, color);
    window_state.bgcolor.a = 255;

    return color;
}

mrb_value nx_window_draw_pixel(mrb_state *mrb, mrb_value self) {
    mrb_float x, y;
    mrb_value color;
    mrb_float z = 0.0;
    // 引数：x, y, [R, G, B], (z)
    mrb_get_args(mrb, "ffA|f", &x, &y, &color, &z);

    DrawCmd cmd;
    cmd.type  = CMD_PIXEL;
    cmd.z     = (float)z;
    cmd.color = nx_window_parse_color(mrb, color);

    cmd.data.pixel.x = (float)x - window_state.ox;
    cmd.data.pixel.y = (float)y - window_state.oy;

    nx_window_push_cmd(cmd);
    return mrb_nil_value();
}

mrb_value nx_window_draw_line(mrb_state *mrb, mrb_value self) {
    mrb_float x1, y1, x2, y2;
    mrb_value color;
    mrb_float z = 0.0;
    // 引数：x1, y1, x2, y2, [R, G, B], (z)
    mrb_get_args(mrb, "ffffA|f", &x1, &y1, &x2, &y2, &color, &z);

    DrawCmd cmd;
    cmd.type  = CMD_LINE;
    cmd.z     = (float)z;
    cmd.color = nx_window_parse_color(mrb, color);

    cmd.data.line.x1 = (float)x1 - window_state.ox;
    cmd.data.line.y1 = (float)y1 - window_state.oy;
    cmd.data.line.x2 = (float)x2 - window_state.ox;
    cmd.data.line.y2 = (float)y2 - window_state.oy;

    nx_window_push_cmd(cmd);
    return mrb_nil_value();
}

mrb_value nx_window_draw_rect(mrb_state *mrb, mrb_value self) {
    mrb_float x, y, w, h;
    mrb_value color;
    mrb_float z = 0.0;
    // 引数：x, y, w, h, [R, G, B], (z)
    mrb_get_args(mrb, "ffffA|f", &x, &y, &w, &h, &color, &z);

    DrawCmd cmd;
    cmd.type  = CMD_RECT;
    cmd.z     = (float)z;
    cmd.color = nx_window_parse_color(mrb, color);

    cmd.data.rect.x = (float)x - window_state.ox;
    cmd.data.rect.y = (float)y - window_state.oy;
    cmd.data.rect.w = (float)w;
    cmd.data.rect.h = (float)h;
        
    nx_window_push_cmd(cmd);
    return mrb_nil_value();
}

mrb_value nx_window_draw_rect_fill(mrb_state *mrb, mrb_value self) {
    mrb_float x, y, w, h;
    mrb_value color;
    mrb_float z = 0.0;
    // 引数：x, y, w, h, [R, G, B], (z)
    mrb_get_args(mrb, "ffffA|f", &x, &y, &w, &h, &color, &z);

    DrawCmd cmd;
    cmd.type  = CMD_RECT_FILL;
    cmd.z     = (float)z;
    cmd.color = nx_window_parse_color(mrb, color);

    cmd.data.rect.x = (float)x - window_state.ox;
    cmd.data.rect.y = (float)y - window_state.oy;
    cmd.data.rect.w = (float)w;
    cmd.data.rect.h = (float)h;
    
    nx_window_push_cmd(cmd);
    return mrb_nil_value();
}

mrb_value nx_window_draw_circle(mrb_state *mrb, mrb_value self) {
    mrb_float cx, cy, cr;
    mrb_value color;
    mrb_float z = 0.0;
    // 引数：中心cx, 中心cy, 半径cy, [R, G, B], (z)
    mrb_get_args(mrb, "fffA|f", &cx, &cy, &cr, &color, &z);
    
    if (cr < 0) return mrb_nil_value();
    
    // 半径0の場合は1ピクセルだけ打って終了
    if (cr == 0) {
        DrawCmd cmd;
        cmd.type  = CMD_PIXEL;
        cmd.z     = (float)z;

        cmd.data.pixel.x = (float)cx - window_state.ox;
        cmd.data.pixel.y = (float)cy - window_state.oy;

        cmd.color = nx_window_parse_color(mrb, color);
        nx_window_push_cmd(cmd);
        return mrb_nil_value();
    } else {    // そうでなければ円
        DrawCmd cmd;
        cmd.type  = CMD_CIRCLE;
        cmd.z     = (float)z;
        cmd.color = nx_window_parse_color(mrb, color);

        cmd.data.circle.cx = (float)cx - window_state.ox;
        cmd.data.circle.cy = (float)cy - window_state.oy;
        cmd.data.circle.cr = (float)cr;

        nx_window_push_cmd(cmd);
        return mrb_nil_value();
    }

    return mrb_nil_value();
}

mrb_value nx_window_draw_circle_fill(mrb_state *mrb, mrb_value self) {
        mrb_float cx, cy, cr;
    mrb_value color;
    mrb_float z = 0.0;
    // 引数：中心cx, 中心cy, 半径cy, [R, G, B], (z)
    mrb_get_args(mrb, "fffA|f", &cx, &cy, &cr, &color, &z);
    
    if (cr < 0) return mrb_nil_value();
    
    if (cr == 0) {
        DrawCmd cmd;
        cmd.type  = CMD_PIXEL;
        cmd.z     = (float)z;

        cmd.data.pixel.x = (float)cx - window_state.ox;
        cmd.data.pixel.y = (float)cy - window_state.oy;

        cmd.color = nx_window_parse_color(mrb, color);
        nx_window_push_cmd(cmd);
        return mrb_nil_value();
    } else {    // そうでなければ円
        DrawCmd cmd;
        cmd.type  = CMD_CIRCLE_FILL;
        cmd.z     = (float)z;
        cmd.color = nx_window_parse_color(mrb, color);

        cmd.data.circle.cx = (float)cx - window_state.ox;
        cmd.data.circle.cy = (float)cy - window_state.oy;
        cmd.data.circle.cr = (float)cr;

        nx_window_push_cmd(cmd);
        return mrb_nil_value();
    }

    return mrb_nil_value();
}
