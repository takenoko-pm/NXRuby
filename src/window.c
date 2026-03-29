#include <mruby.h>
#include <mruby/compile.h>
#include <mruby/array.h>
#include <mruby/hash.h>
#include <mruby/error.h>
#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdlib.h>
#include <math.h>
#include "window.h"
#include "input.h"
#include "image.h"
#include "font.h"

// ================================================================================
// [1] データ定義と内部状態
// ================================================================================

// 合成モードの定義
typedef enum {
    BLEND_ALPHA, // 通常（半透明）
    BLEND_ADD,   // 加算合成（光る）
    BLEND_SUB,   // 減算合成（影・暗闇）
    BLEND_MOD    // 乗算合成（色を掛け合わせる）
} NxBlendMode;

// 描画コマンドキューの定義
typedef enum {
    CMD_PIXEL,
    CMD_LINE,
    CMD_RECT,
    CMD_RECT_FILL,
    CMD_CIRCLE,
    CMD_CIRCLE_FILL,
    CMD_IMAGE,
    CMD_IMAGE_EX,
    CMD_TEXT
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
        struct { float  x,  y; SDL_Texture *texture; SDL_FRect src_rect; } image;
        struct { 
            float x, y; 
            SDL_Texture *texture; 
            SDL_FRect src_rect;
            float angle; 
            float scale_x, scale_y; 
            float center_x, center_y; 
            int alpha; 
            NxBlendMode blend; 
        } image_ex;
        struct { float x, y; SDL_Texture *texture; } text;
    } data;
} DrawCmd;

// 描画キュー
#define MAX_DRAW_CMDS 65536
static DrawCmd draw_queue[MAX_DRAW_CMDS];
static int draw_cmd_count = 0;

// ウィンドウ状態を管理する構造体
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
    // フィルター関連
    SDL_ScaleMode min_filter;
    SDL_ScaleMode mag_filter;
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
    .min_filter    = SDL_SCALEMODE_NEAREST,
    .mag_filter    = SDL_SCALEMODE_NEAREST,
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
static LoopState loop_state_instance;

// 減算合成用のカスタムブレンドモード
static SDL_BlendMode blend_mode_sub;

// --- 内部関数の前方宣言 ---
static void nx_window_create(mrb_state *mrb);
static void nx_window_update_fps(Uint64 current_ns);
static bool nx_window_call_ruby_block(void);
static void nx_window_clear_queue(void);
static bool nx_window_push_cmd(DrawCmd cmd);
static void nx_window_draw_queue(void);
static SDL_Color nx_window_parse_color(mrb_state *mrb, mrb_value color);

// ================================================================================
// [2] 内部エンジンロジック
// ================================================================================

// 初期化処理(初回実行時)
static void nx_window_create(mrb_state *mrb) {
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
    window_state.window = SDL_CreateWindow(
        window_state.caption, 
        (int)(window_state.width * window_state.scale), 
        (int)(window_state.height * window_state.scale), 
        flags
    );
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

    blend_mode_sub = SDL_ComposeCustomBlendMode(
        SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_REV_SUBTRACT,
        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD
    );

    SDL_SetRenderLogicalPresentation(window_state.renderer, window_state.width, window_state.height, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderDrawBlendMode(window_state.renderer, SDL_BLENDMODE_BLEND);
    
    SDL_SetRenderVSync(window_state.renderer, 1);   // VSyncを常時ON
    window_state.last_ns = SDL_GetTicksNS();        // タイマーの初期化
    window_state.fps_timer = window_state.last_ns;

    window_state.is_ready = true;
}

// SDL3のコールバック（SDL_AppIterate）から毎フレーム呼ばれる関数
bool nx_window_tick(void) {
    // Window.loop が呼ばれていなければアプリを終了させる
    if (!loop_state || !loop_state->is_registered) {
        SDL_Log("NXRuby Error: Window.loop requires a block.");
        return false;
    }

    Uint64 current_ns = SDL_GetTicksNS();
    nx_window_update_fps(current_ns);

    if (window_state.target_fps > 0) {
        // 通常の固定FPSモード
        Uint64 target_ns     = 1000000000ULL / window_state.target_fps;
        Uint64 frame_time    = current_ns - window_state.last_ns;
        window_state.last_ns = current_ns;

        // ラグの上限キャップ（処理落ち時の暴走ストッパー）
        if (frame_time > 100000000ULL) frame_time = 100000000ULL; 

        window_state.lag_ns += frame_time;

        while (window_state.lag_ns >= target_ns) {
            nx_input_update();
            if (!nx_window_call_ruby_block()) return false;
            window_state.lag_ns -= target_ns;
        }
    } else {
        // fps = 0 (無制限モード) の場合は、毎回必ず1回だけ処理を回す
        window_state.last_ns = current_ns;
        window_state.lag_ns = 0;
        nx_input_update();
        if (!nx_window_call_ruby_block()) return false;
    }
    
    SDL_SetRenderDrawColor(window_state.renderer, window_state.bgcolor.r, window_state.bgcolor.g, window_state.bgcolor.b, 255);
    SDL_RenderClear(window_state.renderer);     // 画面のクリア
    nx_window_draw_queue();                     // キューに溜まった絵をZソートして一気に描画    
    SDL_RenderPresent(window_state.renderer);   // 画面の更新
    window_state.render_count++;

    return true; // 正常なのでループ継続
}

// mrb_protect用のラッパー関数
static mrb_value nx_window_yield_protected(mrb_state *mrb, mrb_value block) {
    return mrb_yield_argv(mrb, block, 0, NULL);
}

// Rubyブロック実行の本体
static bool nx_window_call_ruby_block(void) {
    window_state.logic_count++; // Rubyの処理が回った回数を数える
    nx_window_clear_queue();    // 実行前にキューを空にする
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
static void nx_window_update_fps(Uint64 current_ns) {
    if (current_ns - window_state.fps_timer >= 1000000000ULL) {
        window_state.logic_fps    = window_state.logic_count;
        window_state.render_fps   = window_state.render_count;
        window_state.logic_count  = 0;
        window_state.render_count = 0;
        window_state.fps_timer    = current_ns;
    }
}

// ウィンドウ拡大時のマウス座標変換用関数
void nx_window_convert_event(SDL_Event *event) {
    if (window_state.renderer) {
        // イベント内の物理座標を、論理座標（NXRubyの描画座標）に自動変換する
        SDL_ConvertEventToRenderCoordinates(window_state.renderer, event);
    }
}

void nx_window_set_mouse_pos(float x, float y) {
    // ウィンドウがまだ無ければ何もしない
    if (!window_state.window || !window_state.renderer) return;

    float window_x, window_y;
    // 論理座標(NXRuby上の座標)を、物理座標(実際のウィンドウ座標)に変換
    if (SDL_RenderCoordinatesToWindow(window_state.renderer, x, y, &window_x, &window_y)) {
        // SDL3の関数でマウスカーソルをワープさせる
        SDL_WarpMouseInWindow(window_state.window, window_x, window_y);
    }
}

// レンダラー共有用
SDL_Renderer* nx_window_get_renderer(void) {
    return window_state.renderer;
}

// ================================================================================
// [3] 内部描画ロジック
// ================================================================================

// 描画キューをリセットする際に、使い捨ての文字テクスチャを破棄する
static void nx_window_clear_queue(void) {
    for (int i = 0; i < draw_cmd_count; i++) {
        // テキストコマンドで、かつテクスチャが生きている場合のみ破棄
        if (draw_queue[i].type == CMD_TEXT && draw_queue[i].data.text.texture) {
            SDL_DestroyTexture(draw_queue[i].data.text.texture);
            draw_queue[i].data.text.texture = NULL; // 安全のため空にしておく
        }
    }
    draw_cmd_count = 0; // ここでキューを空にする
}

static bool nx_window_push_cmd(DrawCmd cmd) {
    if (draw_cmd_count < MAX_DRAW_CMDS) {
        cmd.order = draw_cmd_count;                 // キューに入った順番を記録
        draw_queue[draw_cmd_count++] = cmd;
        return true;
    } else {
        static bool warned = false;
        if (!warned) {
            SDL_Log("NXRuby Warning: Draw command queue overflow! Max %d", MAX_DRAW_CMDS);
            warned = true;
        }
        return true;
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
            case CMD_IMAGE: {
                if (cmd->data.image.texture) {
                    SDL_SetTextureScaleMode(cmd->data.image.texture, window_state.mag_filter);
                    // dst_rect の幅・高さは、テクスチャの元のサイズではなく、スライスされた src_rect のサイズを使う
                    SDL_FRect dst_rect = {
                        cmd->data.image.x, cmd->data.image.y, 
                        cmd->data.image.src_rect.w, cmd->data.image.src_rect.h
                    };
                    // 第3引数に src_rect を渡す
                    SDL_RenderTexture(window_state.renderer, cmd->data.image.texture, &cmd->data.image.src_rect, &dst_rect);
                }
                break;
            }
            case CMD_IMAGE_EX: {
                if (cmd->data.image_ex.texture) {
                    SDL_SetTextureScaleMode(cmd->data.image_ex.texture, window_state.mag_filter);

                    SDL_BlendMode mode = SDL_BLENDMODE_BLEND;
                    if (cmd->data.image_ex.blend == BLEND_ADD) mode = SDL_BLENDMODE_ADD;
                    else if (cmd->data.image_ex.blend == BLEND_SUB) mode = blend_mode_sub;
                    else if (cmd->data.image_ex.blend == BLEND_MOD) mode = SDL_BLENDMODE_MOD;
                    SDL_SetTextureBlendMode(cmd->data.image_ex.texture, mode);
                    SDL_SetTextureAlphaMod(cmd->data.image_ex.texture, (Uint8)cmd->data.image_ex.alpha);

                    float scaled_w = cmd->data.image_ex.src_rect.w * cmd->data.image_ex.scale_x;
                    float scaled_h = cmd->data.image_ex.src_rect.h * cmd->data.image_ex.scale_y;
                    
                    SDL_FRect dst_rect;
                    dst_rect.x = cmd->data.image_ex.x + cmd->data.image_ex.center_x - (cmd->data.image_ex.center_x * cmd->data.image_ex.scale_x);
                    dst_rect.y = cmd->data.image_ex.y + cmd->data.image_ex.center_y - (cmd->data.image_ex.center_y * cmd->data.image_ex.scale_y);
                    dst_rect.w = scaled_w;
                    dst_rect.h = scaled_h;

                    SDL_FPoint center = {
                        cmd->data.image_ex.center_x * cmd->data.image_ex.scale_x,
                        cmd->data.image_ex.center_y * cmd->data.image_ex.scale_y
                    };

                    SDL_RenderTextureRotated(window_state.renderer, cmd->data.image_ex.texture, &cmd->data.image_ex.src_rect, &dst_rect, cmd->data.image_ex.angle, &center, SDL_FLIP_NONE);

                    SDL_SetTextureAlphaMod(cmd->data.image_ex.texture, 255);
                    SDL_SetTextureBlendMode(cmd->data.image_ex.texture, SDL_BLENDMODE_BLEND);
                }
                break;
            }
            case CMD_TEXT: {
                if (cmd->data.text.texture) {
                    float w, h;
                    SDL_GetTextureSize(cmd->data.text.texture, &w, &h);
                    SDL_FRect dst_rect = { cmd->data.text.x, cmd->data.text.y, w, h };
                    
                    SDL_RenderTexture(window_state.renderer, cmd->data.text.texture, NULL, &dst_rect);
                }
                break;
            }
            default: break;
        }
    }
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

// ================================================================================
// [4] Ruby API: ウィンドウ管理
// ================================================================================

static mrb_value nx_window_loop(mrb_state *mrb, mrb_value self) {
    // すでにループが登録されている場合は例外を発生させる
    if (loop_state && loop_state->is_registered) {
        // E_RUNTIME_ERROR は Ruby の RuntimeError に相当
        mrb_raise(mrb, E_RUNTIME_ERROR, "Window.loop can only be called once!");
    }

    mrb_value block;
    mrb_get_args(mrb, "&", &block);     // Ruby側からブロック（do...end）を受け取る
    if (mrb_nil_p(block)) {             // ブロックがなければエラー
        mrb_raise(mrb, E_ARGUMENT_ERROR, "Window.loop requires a block (do...end)");
    }

    // コンテキストの確保（ループ毎に初期化）
    loop_state                = &loop_state_instance;
    loop_state->mrb           = mrb;
    loop_state->block         = block;
    loop_state->is_registered = true;
    
    mrb_gc_register(mrb, block);        // Rubyのブロックがガベージコレクションで消されるのを防ぐ
    SDL_ShowWindow(window_state.window);

    return mrb_nil_value();             // 正常終了
}

// ユーザーが記述する終了処理
static mrb_value nx_window_close(mrb_state *mrb, mrb_value self) {
    // SDLに「アプリを終了して(QUIT)」というイベントを自作して送信する
    SDL_Event event;
    SDL_zero(event);        // 構造体をゼロクリア（安全のため）
    event.type = SDL_EVENT_QUIT;
    
    SDL_PushEvent(&event);  // イベントキューに押し込む

    return mrb_nil_value();
}

static mrb_value nx_window_logic_fps(mrb_state *mrb, mrb_value self) {
    return mrb_int_value(mrb, window_state.logic_fps);
}

static mrb_value nx_window_render_fps(mrb_state *mrb, mrb_value self) {
    return mrb_int_value(mrb, window_state.render_fps);
}

static mrb_value nx_window_fps(mrb_state *mrb, mrb_value self) {
    return mrb_int_value(mrb, window_state.target_fps);
}

static mrb_value nx_window_set_fps(mrb_state *mrb, mrb_value self) {
    mrb_int fps;
    mrb_get_args(mrb, "i", &fps);
    if (fps < 0) fps = 0; // マイナスは0（無制限）として扱う
    
    window_state.target_fps = (int)fps;
    return mrb_int_value(mrb, fps);
}

static mrb_value nx_window_caption(mrb_state *mrb, mrb_value self) {
    return mrb_str_new_cstr(mrb, window_state.caption);
}

static mrb_value nx_window_set_caption(mrb_state *mrb, mrb_value self) {
    char *str;
    mrb_get_args(mrb, "z", &str);

    // バッファオーバーランを防ぎつつ構造体の配列にコピー
    snprintf(window_state.caption, sizeof(window_state.caption), "%s", str);

    // ウィンドウが生成済みならタイトルを更新する
    if (window_state.window) SDL_SetWindowTitle(window_state.window, window_state.caption);

    return mrb_str_new_cstr(mrb, window_state.caption);
}

static mrb_value nx_window_width(mrb_state *mrb, mrb_value self) {
    return mrb_int_value(mrb, window_state.width);
}

static mrb_value nx_window_set_width(mrb_state *mrb, mrb_value self) {
    mrb_int w;
    mrb_get_args(mrb, "i", &w);
    if (w < 1) w = 1; // 最小サイズ制限

    window_state.width = (int)w;

    // もし既にウィンドウが作られていれば、実体をリサイズする
    if (window_state.window) {
        SDL_SetWindowSize(window_state.window, 
            (int)(window_state.width * window_state.scale), 
            (int)(window_state.height * window_state.scale));

            // 論理解像度も新しい width/height で更新する
            SDL_SetRenderLogicalPresentation(window_state.renderer, window_state.width, window_state.height, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    }
    return mrb_int_value(mrb, w);
}

static mrb_value nx_window_height(mrb_state *mrb, mrb_value self) {
    return mrb_int_value(mrb, window_state.height);
}

static mrb_value nx_window_set_height(mrb_state *mrb, mrb_value self) {
    mrb_int h;
    mrb_get_args(mrb, "i", &h);
    if (h < 1) h = 1; // 最小サイズ制限

    window_state.height = (int)h;

    // もし既にウィンドウが作られていれば、実体をリサイズする
    if (window_state.window) {
        SDL_SetWindowSize(window_state.window, 
            (int)(window_state.width * window_state.scale), 
            (int)(window_state.height * window_state.scale));

            // 論理解像度も新しい width/height で更新する
            SDL_SetRenderLogicalPresentation(window_state.renderer, window_state.width, window_state.height, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    }
    return mrb_int_value(mrb, h);
}

static mrb_value nx_window_x(mrb_state *mrb, mrb_value self) {
    if (window_state.window) {
        SDL_GetWindowPosition(window_state.window, &window_state.x, &window_state.y);
    }
    return mrb_int_value(mrb, window_state.x);
}

static mrb_value nx_window_set_x(mrb_state *mrb, mrb_value self) {
    mrb_int x;
    mrb_get_args(mrb, "i", &x);
    window_state.x = (int)x;
    if (window_state.window) {
        SDL_SetWindowPosition(window_state.window, window_state.x, window_state.y);
    }
    return mrb_int_value(mrb, x);
}

static mrb_value nx_window_y(mrb_state *mrb, mrb_value self) {
    if (window_state.window) {
        SDL_GetWindowPosition(window_state.window, &window_state.x, &window_state.y);
    }
    return mrb_int_value(mrb, window_state.y);
}

static mrb_value nx_window_set_y(mrb_state *mrb, mrb_value self) {
    mrb_int y;
    mrb_get_args(mrb, "i", &y);
    window_state.y = (int)y;
    if (window_state.window) {
        SDL_SetWindowPosition(window_state.window, window_state.x, window_state.y);
    }
    return mrb_int_value(mrb, y);
}

static mrb_value nx_window_full_screen(mrb_state *mrb, mrb_value self) {
    return mrb_bool_value(window_state.is_fullscreen);
}

static mrb_value nx_window_set_full_screen(mrb_state *mrb, mrb_value self) {
    mrb_bool b;
    mrb_get_args(mrb, "b", &b);
    window_state.is_fullscreen = b;
    if (window_state.window) {
        SDL_SetWindowFullscreen(window_state.window, b);
    }
    return mrb_bool_value(b);
}

static mrb_value nx_window_windowed(mrb_state *mrb, mrb_value self) {
    return mrb_bool_value(!window_state.is_fullscreen);
}

static mrb_value nx_window_set_windowed(mrb_state *mrb, mrb_value self) {
    mrb_bool b;
    mrb_get_args(mrb, "b", &b);
    window_state.is_fullscreen = !b;
    if (window_state.window) {
        SDL_SetWindowFullscreen(window_state.window, !b);
    }
    return mrb_bool_value(b);
}

static mrb_value nx_window_running_time(mrb_state *mrb, mrb_value self) {
    return mrb_int_value(mrb, SDL_GetTicks());
}

static mrb_value nx_window_scale(mrb_state *mrb, mrb_value self) {
    return mrb_float_value(mrb, window_state.scale);
}

static mrb_value nx_window_set_scale(mrb_state *mrb, mrb_value self) {
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

static mrb_value nx_window_active(mrb_state *mrb, mrb_value self) {
    if (!window_state.window) return mrb_false_value();
    
    // ウィンドウが現在キーボード入力を受け付けているか（最前面か）を判定
    SDL_WindowFlags flags = SDL_GetWindowFlags(window_state.window);
    return mrb_bool_value((flags & SDL_WINDOW_INPUT_FOCUS) != 0);
}

static mrb_value nx_window_ox(mrb_state *mrb, mrb_value self) {
    return mrb_float_value(mrb, window_state.ox);
}

static mrb_value nx_window_set_ox(mrb_state *mrb, mrb_value self) {
    mrb_float ox;
    mrb_get_args(mrb, "f", &ox);
    window_state.ox = (float)ox;
    return mrb_float_value(mrb, ox);
}

static mrb_value nx_window_oy(mrb_state *mrb, mrb_value self) {
    return mrb_float_value(mrb, window_state.oy);
}

static mrb_value nx_window_set_oy(mrb_state *mrb, mrb_value self) {
    mrb_float oy;
    mrb_get_args(mrb, "f", &oy);
    window_state.oy = (float)oy;
    return mrb_float_value(mrb, oy);
}

static mrb_value nx_window_bgcolor(mrb_state *mrb, mrb_value self) {
    mrb_value color[4];
    
    // 構造体の数値を配列に代入
    color[0] = mrb_int_value(mrb, window_state.bgcolor.r);
    color[1] = mrb_int_value(mrb, window_state.bgcolor.g);
    color[2] = mrb_int_value(mrb, window_state.bgcolor.b);
    color[3] = mrb_int_value(mrb, window_state.bgcolor.a);

    // Rubyの配列として返す
    return mrb_ary_new_from_values(mrb, 4, color);
}

static mrb_value nx_window_set_bgcolor(mrb_state *mrb, mrb_value self) {
    mrb_value color;
    mrb_get_args(mrb, "A", &color);

    // color配列の処理
    window_state.bgcolor   = nx_window_parse_color(mrb, color);
    window_state.bgcolor.a = 255;

    return color;
}

static mrb_value nx_window_min_filter(mrb_state *mrb, mrb_value self) {
    return mrb_int_value(mrb, window_state.min_filter);
}

static mrb_value nx_window_set_min_filter(mrb_state *mrb, mrb_value self) {
    mrb_int filter;
    mrb_get_args(mrb, "i", &filter);
    window_state.min_filter = (SDL_ScaleMode)filter;
    return mrb_int_value(mrb, filter);
}

static mrb_value nx_window_mag_filter(mrb_state *mrb, mrb_value self) {
    return mrb_int_value(mrb, window_state.mag_filter);
}

static mrb_value nx_window_set_mag_filter(mrb_state *mrb, mrb_value self) {
    mrb_int filter;
    mrb_get_args(mrb, "i", &filter);
    window_state.mag_filter = (SDL_ScaleMode)filter;
    return mrb_int_value(mrb, filter);
}

// ================================================================================
// [5] Ruby API: 描画コマンド
// ================================================================================

static mrb_value nx_window_draw_pixel(mrb_state *mrb, mrb_value self) {
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

static mrb_value nx_window_draw_line(mrb_state *mrb, mrb_value self) {
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

static mrb_value nx_window_draw_rect(mrb_state *mrb, mrb_value self) {
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

static mrb_value nx_window_draw_rect_fill(mrb_state *mrb, mrb_value self) {
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

static mrb_value nx_window_draw_circle(mrb_state *mrb, mrb_value self) {
    mrb_float cx, cy, cr;
    mrb_value color;
    mrb_float z = 0.0;
    // 引数：中心cx, 中心cy, 半径cy, [R, G, B], (z)
    mrb_get_args(mrb, "fffA|f", &cx, &cy, &cr, &color, &z);
    
    if (cr < 0) return mrb_nil_value();
    DrawCmd cmd;
    cmd.z     = (float)z;
    cmd.color = nx_window_parse_color(mrb, color);

    // 半径0の場合は1ピクセルだけ打って終了
    if (cr == 0) {
        cmd.type           = CMD_PIXEL;
        cmd.data.pixel.x   = (float)cx - window_state.ox;
        cmd.data.pixel.y   = (float)cy - window_state.oy;
    } else {    // そうでなければ円
        cmd.type           = CMD_CIRCLE;
        cmd.data.circle.cx = (float)cx - window_state.ox;
        cmd.data.circle.cy = (float)cy - window_state.oy;
        cmd.data.circle.cr = (float)cr;
    }

    nx_window_push_cmd(cmd);
    return mrb_nil_value();
}

static mrb_value nx_window_draw_circle_fill(mrb_state *mrb, mrb_value self) {
    mrb_float cx, cy, cr;
    mrb_value color;
    mrb_float z = 0.0;
    // 引数：中心cx, 中心cy, 半径cy, [R, G, B], (z)
    mrb_get_args(mrb, "fffA|f", &cx, &cy, &cr, &color, &z);
    
    if (cr < 0) return mrb_nil_value();
    DrawCmd cmd;
    cmd.z     = (float)z;
    cmd.color = nx_window_parse_color(mrb, color);

    if (cr == 0) {
        cmd.type           = CMD_PIXEL;
        cmd.data.pixel.x   = (float)cx - window_state.ox;
        cmd.data.pixel.y   = (float)cy - window_state.oy;
    } else {    // そうでなければ円
        cmd.type           = CMD_CIRCLE_FILL;
        cmd.data.circle.cx = (float)cx - window_state.ox;
        cmd.data.circle.cy = (float)cy - window_state.oy;
        cmd.data.circle.cr = (float)cr;
    }

    nx_window_push_cmd(cmd);
    return mrb_nil_value();
}

// --- Window.draw ---
static mrb_value nx_window_draw(mrb_state *mrb, mrb_value self) {
    mrb_float x, y;
    mrb_value image_obj;
    mrb_float z = 0.0;
    mrb_get_args(mrb, "ffo|f", &x, &y, &image_obj, &z);

    NxImage *img_data = nx_image_get_data(mrb, image_obj);
    if (!img_data || !img_data->shared_tex || !img_data->shared_tex->texture) mrb_raise(mrb, E_ARGUMENT_ERROR, "Invalid Image object");

    DrawCmd cmd;
    cmd.type  = CMD_IMAGE;
    cmd.z     = (float)z;
    cmd.color = (SDL_Color){255, 255, 255, 255}; 
    
    cmd.data.image.texture = img_data->shared_tex->texture;
    cmd.data.image.src_rect = img_data->src_rect;
    cmd.data.image.x = (float)x - window_state.ox;
    cmd.data.image.y = (float)y - window_state.oy;

    nx_window_push_cmd(cmd);
    return mrb_nil_value();
}

// --- 拡張描画(EX)コマンドの共通セットアップ関数 ---
static bool nx_window_setup_ex_cmd(mrb_state *mrb, mrb_value image_obj, float x, float y, float z, DrawCmd *cmd) {
    NxImage *img_data = nx_image_get_data(mrb, image_obj);
    if (!img_data || !img_data->shared_tex || !img_data->shared_tex->texture) return false;

    cmd->type = CMD_IMAGE_EX;
    cmd->z = z;
    cmd->data.image_ex.texture = img_data->shared_tex->texture;
    cmd->data.image_ex.src_rect = img_data->src_rect; // ★追加
    cmd->data.image_ex.x = x - window_state.ox;
    cmd->data.image_ex.y = y - window_state.oy;
    
    cmd->data.image_ex.angle = 0.0f;
    cmd->data.image_ex.scale_x = 1.0f;
    cmd->data.image_ex.scale_y = 1.0f;
    
    // ★画像全体のサイズではなく、切り取られた幅・高さを中心点にする
    cmd->data.image_ex.center_x = img_data->width / 2.0f;
    cmd->data.image_ex.center_y = img_data->height / 2.0f;
    
    cmd->data.image_ex.alpha = 255;
    cmd->data.image_ex.blend = BLEND_ALPHA;

    return true;
}

// --- Window.draw_ex(x, y, image, options={}) ---
static mrb_value nx_window_draw_ex(mrb_state *mrb, mrb_value self) {
    mrb_float x, y;
    mrb_value image_obj;
    mrb_value options = mrb_nil_value(); 
    
    mrb_get_args(mrb, "ffo|H", &x, &y, &image_obj, &options);

    DrawCmd cmd;
    if (!nx_window_setup_ex_cmd(mrb, image_obj, (float)x, (float)y, 0.0f, &cmd)) {
        return mrb_nil_value();
    }

    // Hashオプションが渡されていれば上書きする（ここはそのまま）
    if (!mrb_nil_p(options)) {
        mrb_value val;
        
        val = mrb_hash_get(mrb, options, mrb_symbol_value(mrb_intern_cstr(mrb, "z")));
        if (!mrb_nil_p(val)) cmd.z = (float)mrb_as_float(mrb, val);

        val = mrb_hash_get(mrb, options, mrb_symbol_value(mrb_intern_cstr(mrb, "angle")));
        if (!mrb_nil_p(val)) cmd.data.image_ex.angle = (float)mrb_as_float(mrb, val);

        val = mrb_hash_get(mrb, options, mrb_symbol_value(mrb_intern_cstr(mrb, "scale_x")));
        if (!mrb_nil_p(val)) cmd.data.image_ex.scale_x = (float)mrb_as_float(mrb, val);

        val = mrb_hash_get(mrb, options, mrb_symbol_value(mrb_intern_cstr(mrb, "scale_y")));
        if (!mrb_nil_p(val)) cmd.data.image_ex.scale_y = (float)mrb_as_float(mrb, val);

        val = mrb_hash_get(mrb, options, mrb_symbol_value(mrb_intern_cstr(mrb, "center_x")));
        if (!mrb_nil_p(val)) cmd.data.image_ex.center_x = (float)mrb_as_float(mrb, val);

        val = mrb_hash_get(mrb, options, mrb_symbol_value(mrb_intern_cstr(mrb, "center_y")));
        if (!mrb_nil_p(val)) cmd.data.image_ex.center_y = (float)mrb_as_float(mrb, val);

        val = mrb_hash_get(mrb, options, mrb_symbol_value(mrb_intern_cstr(mrb, "alpha")));
        if (!mrb_nil_p(val)) cmd.data.image_ex.alpha = (int)mrb_as_int(mrb, val);

        val = mrb_hash_get(mrb, options, mrb_symbol_value(mrb_intern_cstr(mrb, "blend")));
        if (mrb_symbol_p(val)) {
            const char *blend_name = mrb_sym_name(mrb, mrb_symbol(val));
            if (strcmp(blend_name, "add") == 0) cmd.data.image_ex.blend = BLEND_ADD;
            else if (strcmp(blend_name, "sub") == 0) cmd.data.image_ex.blend = BLEND_SUB;
            else if (strcmp(blend_name, "mod") == 0) cmd.data.image_ex.blend = BLEND_MOD;
            else if (strcmp(blend_name, "none") == 0) cmd.data.image_ex.blend = BLEND_ALPHA; 
        }
    }

    nx_window_push_cmd(cmd);
    return mrb_nil_value();
}

// --- Window.draw_rot(x, y, image, angle, center_x=nil, center_y=nil, z=0) ---
static mrb_value nx_window_draw_rot(mrb_state *mrb, mrb_value self) {
    mrb_float x, y, angle;
    mrb_value img;
    mrb_float cx = -9999.0, cy = -9999.0, z = 0.0; // 省略判定用のマジックナンバー
    mrb_get_args(mrb, "ffof|fff", &x, &y, &img, &angle, &cx, &cy, &z);

    DrawCmd cmd;
    if (nx_window_setup_ex_cmd(mrb, img, (float)x, (float)y, (float)z, &cmd)) {
        cmd.data.image_ex.angle = (float)angle;
        if (cx != -9999.0) cmd.data.image_ex.center_x = (float)cx;
        if (cy != -9999.0) cmd.data.image_ex.center_y = (float)cy;
        nx_window_push_cmd(cmd);
    }
    return mrb_nil_value();
}

// --- Window.draw_scale(x, y, image, scale_x, scale_y, center_x=nil, center_y=nil, z=0) ---
static mrb_value nx_window_draw_scale(mrb_state *mrb, mrb_value self) {
    mrb_float x, y, sx, sy;
    mrb_value img;
    mrb_float cx = -9999.0, cy = -9999.0, z = 0.0;
    mrb_get_args(mrb, "ffoff|fff", &x, &y, &img, &sx, &sy, &cx, &cy, &z);

    DrawCmd cmd;
    if (nx_window_setup_ex_cmd(mrb, img, (float)x, (float)y, (float)z, &cmd)) {
        cmd.data.image_ex.scale_x = (float)sx;
        cmd.data.image_ex.scale_y = (float)sy;
        if (cx != -9999.0) cmd.data.image_ex.center_x = (float)cx;
        if (cy != -9999.0) cmd.data.image_ex.center_y = (float)cy;
        nx_window_push_cmd(cmd);
    }
    return mrb_nil_value();
}

// --- Window.draw_alpha(x, y, image, alpha, z=0) ---
static mrb_value nx_window_draw_alpha(mrb_state *mrb, mrb_value self) {
    mrb_float x, y;
    mrb_int alpha;
    mrb_value img;
    mrb_float z = 0.0;
    mrb_get_args(mrb, "ffoi|f", &x, &y, &img, &alpha, &z);

    DrawCmd cmd;
    if (nx_window_setup_ex_cmd(mrb, img, (float)x, (float)y, (float)z, &cmd)) {
        cmd.data.image_ex.alpha = (int)alpha;
        nx_window_push_cmd(cmd);
    }
    return mrb_nil_value();
}

// --- Window.draw_add(x, y, image, z=0) ---
static mrb_value nx_window_draw_add(mrb_state *mrb, mrb_value self) {
    mrb_float x, y;
    mrb_value img;
    mrb_float z = 0.0;
    mrb_get_args(mrb, "ffo|f", &x, &y, &img, &z);

    DrawCmd cmd;
    if (nx_window_setup_ex_cmd(mrb, img, (float)x, (float)y, (float)z, &cmd)) {
        cmd.data.image_ex.blend = BLEND_ADD;
        nx_window_push_cmd(cmd);
    }
    return mrb_nil_value();
}

// --- Window.draw_sub(x, y, image, z=0) ---
static mrb_value nx_window_draw_sub(mrb_state *mrb, mrb_value self) {
    mrb_float x, y;
    mrb_value img;
    mrb_float z = 0.0;
    mrb_get_args(mrb, "ffo|f", &x, &y, &img, &z);

    DrawCmd cmd;
    if (nx_window_setup_ex_cmd(mrb, img, (float)x, (float)y, (float)z, &cmd)) {
        cmd.data.image_ex.blend = BLEND_SUB;
        nx_window_push_cmd(cmd);
    }
    return mrb_nil_value();
}

// --- Window.draw_mod(x, y, image, z=0) ---
static mrb_value nx_window_draw_mod(mrb_state *mrb, mrb_value self) {
    mrb_float x, y;
    mrb_value img;
    mrb_float z = 0.0;
    mrb_get_args(mrb, "ffo|f", &x, &y, &img, &z);

    DrawCmd cmd;
    if (nx_window_setup_ex_cmd(mrb, img, (float)x, (float)y, (float)z, &cmd)) {
        cmd.data.image_ex.blend = BLEND_MOD;
        nx_window_push_cmd(cmd);
    }
    return mrb_nil_value();
}

// --- Window.draw_tile(x, y, map, images, x_start=0, y_start=0, x_count=nil, y_count=nil, z=0) ---
static mrb_value nx_window_draw_tile(mrb_state *mrb, mrb_value self) {
    mrb_float x, y;
    mrb_value map_ary, img_ary;
    mrb_value xs_obj = mrb_nil_value(), ys_obj = mrb_nil_value();
    mrb_value xc_obj = mrb_nil_value(), yc_obj = mrb_nil_value();
    mrb_value z_obj  = mrb_nil_value();

    // 引数: x, y, map配列, images配列, [x_start, y_start, x_count, y_count, z]
    mrb_get_args(mrb, "ffAA|ooooo", &x, &y, &map_ary, &img_ary, &xs_obj, &ys_obj, &xc_obj, &yc_obj, &z_obj);

    // デフォルト値の処理
    int x_start = mrb_nil_p(xs_obj) ? 0 : (int)mrb_as_int(mrb, xs_obj);
    int y_start = mrb_nil_p(ys_obj) ? 0 : (int)mrb_as_int(mrb, ys_obj);
    float z     = mrb_nil_p(z_obj)  ? 0.0f : (float)mrb_as_float(mrb, z_obj);

    mrb_int img_len = RARRAY_LEN(img_ary);
    if (img_len == 0) return mrb_nil_value();

    // DXRuby仕様: タイルの基本サイズは image_array[0] のサイズに依存する
    mrb_value first_img_obj = mrb_ary_entry(img_ary, 0);
    NxImage *first_img = nx_image_get_data(mrb, first_img_obj);
    if (!first_img) return mrb_nil_value();
    float tile_w = first_img->width;
    float tile_h = first_img->height;

    mrb_int map_h = RARRAY_LEN(map_ary);
    if (map_h == 0) return mrb_nil_value();

    // 描画する行数（y_count）の決定
    int y_count = mrb_nil_p(yc_obj) ? (int)map_h - y_start : (int)mrb_as_int(mrb, yc_obj);
    if (y_count <= 0) return mrb_nil_value();

    bool has_x_count = !mrb_nil_p(xc_obj);
    int def_x_count  = has_x_count ? (int)mrb_as_int(mrb, xc_obj) : 0;

    // Y軸（行）のループ
    for (int iy = 0; iy < y_count; iy++) {
        int map_y = y_start + iy;
        if (map_y < 0 || map_y >= map_h) continue;

        mrb_value row_obj = mrb_ary_entry(map_ary, map_y);
        if (!mrb_array_p(row_obj)) continue; // 2次元配列の要素が配列じゃなければスキップ

        mrb_int row_len = RARRAY_LEN(row_obj);
        // 描画する列数（x_count）の決定（nilならその行の最後まで）
        int x_count = has_x_count ? def_x_count : (int)row_len - x_start;

        // X軸（列）のループ
        for (int ix = 0; ix < x_count; ix++) {
            int map_x = x_start + ix;
            if (map_x < 0 || map_x >= row_len) continue;

            mrb_value idx_obj = mrb_ary_entry(row_obj, map_x);
            if (mrb_nil_p(idx_obj)) continue; // nilのマスは描画しない（透明扱い）

            mrb_int tile_idx = mrb_as_int(mrb, idx_obj);
            // マイナス値や、画像配列の範囲外のインデックスは描画しない
            if (tile_idx < 0 || tile_idx >= img_len) continue; 

            mrb_value img_obj = mrb_ary_entry(img_ary, tile_idx);
            NxImage *img_data = nx_image_get_data(mrb, img_obj);
            if (!img_data || !img_data->shared_tex || !img_data->shared_tex->texture) continue;

            // キューに積む（最速パスである CMD_IMAGE を使用）
            DrawCmd cmd;
            cmd.type  = CMD_IMAGE;
            cmd.z     = z;
            cmd.color = (SDL_Color){255, 255, 255, 255};
            
            cmd.data.image.texture  = img_data->shared_tex->texture;
            cmd.data.image.src_rect = img_data->src_rect;
            
            // 描画座標 ＝ ベース座標 ＋ (ループ変数 × タイルサイズ) － オフセット(ox, oy)
            cmd.data.image.x = (float)x + (ix * tile_w) - window_state.ox;
            cmd.data.image.y = (float)y + (iy * tile_h) - window_state.oy;

            nx_window_push_cmd(cmd);
        }
    }
    return mrb_nil_value();
}

// --- Window.draw_font(x, y, text, font, options={}) ---
static mrb_value nx_window_draw_font(mrb_state *mrb, mrb_value self) {
    mrb_float x, y;
    const char *text;
    mrb_value font_obj;
    mrb_value options = mrb_nil_value();
    
    // 引数: x, y, 文字列, Fontオブジェクト, [Hash]
    mrb_get_args(mrb, "ffzo|H", &x, &y, &text, &font_obj, &options);

    TTF_Font *ttf_font = nx_font_get_ptr(mrb, font_obj);
    if (!ttf_font) mrb_raise(mrb, E_ARGUMENT_ERROR, "Invalid Font object");

    DrawCmd cmd;
    cmd.type = CMD_TEXT;
    cmd.z = 0.0f;
    cmd.color = (SDL_Color){255, 255, 255, 255}; // デフォルトは白
    
    // Hashオプションの解析
    if (!mrb_nil_p(options)) {
        mrb_value val;
        val = mrb_hash_get(mrb, options, mrb_symbol_value(mrb_intern_cstr(mrb, "z")));
        if (!mrb_nil_p(val)) cmd.z = (float)mrb_as_float(mrb, val);

        val = mrb_hash_get(mrb, options, mrb_symbol_value(mrb_intern_cstr(mrb, "color")));
        if (!mrb_nil_p(val)) cmd.color = nx_window_parse_color(mrb, val);
    }

    // SDL3_ttf で文字を画像（Surface）として生成し、テクスチャに変換する
    // ※ SDL3 では長さ0を指定するとnull終端まで読んでくれます
    SDL_Surface *surface = TTF_RenderText_Blended(ttf_font, text, 0, cmd.color);
    if (!surface) return mrb_nil_value();
    
    SDL_Texture *texture = SDL_CreateTextureFromSurface(window_state.renderer, surface);
    SDL_DestroySurface(surface); // Surfaceは用済みなので破棄
    if (!texture) return mrb_nil_value();

    cmd.data.text.texture = texture;
    cmd.data.text.x = (float)x - window_state.ox;
    cmd.data.text.y = (float)y - window_state.oy;

    // キューが溢れて追加できなかった場合は、テクスチャだけ破棄してメモリを守る
    if (!nx_window_push_cmd(cmd)) {
        SDL_DestroyTexture(texture);
    }
    
    return mrb_nil_value();
}

// ================================================================================
// [6] 初期化とメソッド登録
// ================================================================================

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
    {"min_filter"      , nx_window_min_filter      , MRB_ARGS_NONE()},
    {"min_filter="     , nx_window_set_min_filter  , MRB_ARGS_REQ(1)},
    {"mag_filter"      , nx_window_mag_filter      , MRB_ARGS_NONE()},
    {"mag_filter="     , nx_window_set_mag_filter  , MRB_ARGS_REQ(1)},
    {"draw_pixel"      , nx_window_draw_pixel      , MRB_ARGS_REQ(3) | MRB_ARGS_OPT(1)},
    {"draw_line"       , nx_window_draw_line       , MRB_ARGS_REQ(5) | MRB_ARGS_OPT(1)},
    {"draw_rect"       , nx_window_draw_rect       , MRB_ARGS_REQ(5) | MRB_ARGS_OPT(1)},
    {"draw_rect_fill"  , nx_window_draw_rect_fill  , MRB_ARGS_REQ(5) | MRB_ARGS_OPT(1)},
    {"draw_circle"     , nx_window_draw_circle     , MRB_ARGS_REQ(4) | MRB_ARGS_OPT(1)},
    {"draw_circle_fill", nx_window_draw_circle_fill, MRB_ARGS_REQ(4) | MRB_ARGS_OPT(1)},
    {"draw"            , nx_window_draw            , MRB_ARGS_REQ(3) | MRB_ARGS_OPT(1)},
    {"draw_ex"         , nx_window_draw_ex         , MRB_ARGS_REQ(3) | MRB_ARGS_OPT(1)},
    {"draw_rot"        , nx_window_draw_rot        , MRB_ARGS_REQ(4) | MRB_ARGS_OPT(3)},
    {"draw_scale"      , nx_window_draw_scale      , MRB_ARGS_REQ(5) | MRB_ARGS_OPT(3)},
    {"draw_alpha"      , nx_window_draw_alpha      , MRB_ARGS_REQ(4) | MRB_ARGS_OPT(1)},
    {"draw_add"        , nx_window_draw_add        , MRB_ARGS_REQ(3) | MRB_ARGS_OPT(1)},
    {"draw_sub"        , nx_window_draw_sub        , MRB_ARGS_REQ(3) | MRB_ARGS_OPT(1)},
    {"draw_mod"        , nx_window_draw_mod        , MRB_ARGS_REQ(3) | MRB_ARGS_OPT(1)},
    {"draw_tile"       , nx_window_draw_tile       , MRB_ARGS_REQ(4) | MRB_ARGS_OPT(5)},
    {"draw_font"       , nx_window_draw_font       , MRB_ARGS_REQ(4) | MRB_ARGS_OPT(1)},

    // 終端マーク
    {NULL, NULL, 0} 
};

// モジュール・メソッド 登録
void nx_window_init(mrb_state *mrb) {
    struct RClass *Window = mrb_define_module(mrb, "Window");

    mrb_define_const(mrb, mrb->object_class, "TEXF_POINT",  mrb_int_value(mrb, SDL_SCALEMODE_NEAREST));
    mrb_define_const(mrb, mrb->object_class, "TEXF_LINEAR", mrb_int_value(mrb, SDL_SCALEMODE_LINEAR));

    for (int i = 0; window_methods[i].name; i++) {
        mrb_define_module_function(mrb, Window, window_methods[i].name, window_methods[i].func, window_methods[i].aspec);
    }

    nx_window_create(mrb);
}

// アプリ終了時（SDL_AppQuit）に呼ばれる後片付け
void nx_window_cleanup(void) {
    nx_window_clear_queue();
    if (loop_state) {
        // mrb_close()によるシャットダウン中のため、mrb_freeやmrb_gc_unregisterは呼ばない
        loop_state = NULL;
    }
    if (window_state.renderer) SDL_DestroyRenderer(window_state.renderer);
    if (window_state.window)   SDL_DestroyWindow(window_state.window);
    window_state.is_ready = false;
}
