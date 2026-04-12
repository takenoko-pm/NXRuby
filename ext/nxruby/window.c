#include <ruby.h>
#include <SDL.h>
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

typedef enum {
    BLEND_ALPHA,
    BLEND_ADD,
    BLEND_SUB,
    BLEND_MOD
} NxBlendMode;

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
    float     z;              
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

#define MAX_DRAW_CMDS 65536
static DrawCmd draw_queue[MAX_DRAW_CMDS];
static int draw_cmd_count = 0;

// ウィンドウ状態を管理する構造体
typedef struct {
    bool          is_ready;
    SDL_Window   *window;
    SDL_Renderer *renderer;
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
    SDL_ScaleMode min_filter;
    SDL_ScaleMode mag_filter;
    
    // FPS関連
    Uint64        last_ns;
    Uint64        lag_ns;
    int           target_fps;
    int           logic_fps;
    int           logic_count;
    int           render_fps;
    int           render_count;
    Uint64        fps_timer_ns;
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
    .fps_timer_ns  = 0
};

static SDL_BlendMode blend_mode_sub;

static void nx_window_create(void);
static void nx_window_update_fps(Uint64 current_ns);
static void nx_window_clear_queue(void);
static bool nx_window_push_cmd(DrawCmd cmd);
static void nx_window_draw_queue(void);
static SDL_Color nx_window_parse_color(VALUE color);
static bool nx_window_setup_ex_cmd(VALUE image_obj, float x, float y, float z, DrawCmd *cmd);

// --- ヘルパー: ナノ秒を取得する ---
static Uint64 get_ticks_ns(void) {
    return (SDL_GetPerformanceCounter() * 1000000000ULL) / SDL_GetPerformanceFrequency();
}

// ================================================================================
// [2] 内部エンジンロジック
// ================================================================================

static void nx_window_create(void) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        rb_raise(rb_eRuntimeError, "SDL_Init failed: %s", SDL_GetError());
    }

    Uint32 flags = SDL_WINDOW_HIDDEN;
    if (window_state.is_fullscreen) flags |= SDL_WINDOW_FULLSCREEN;

    window_state.window = SDL_CreateWindow(
        window_state.caption, 
        window_state.x, window_state.y, 
        (int)(window_state.width * window_state.scale), 
        (int)(window_state.height * window_state.scale), 
        flags
    );
    if (!window_state.window) {
        rb_raise(rb_eRuntimeError, "CreateWindow failed: %s", SDL_GetError());
    }

    window_state.renderer = SDL_CreateRenderer(window_state.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!window_state.renderer) {
        SDL_DestroyWindow(window_state.window);
        rb_raise(rb_eRuntimeError, "CreateRenderer failed: %s", SDL_GetError());
    }

    blend_mode_sub = SDL_ComposeCustomBlendMode(
        SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_REV_SUBTRACT,
        SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD
    );

    SDL_RenderSetLogicalSize(window_state.renderer, window_state.width, window_state.height);
    SDL_SetRenderDrawBlendMode(window_state.renderer, SDL_BLENDMODE_BLEND);
    
    window_state.last_ns = get_ticks_ns();
    window_state.fps_timer_ns = window_state.last_ns;

    window_state.is_ready = true;
}

static void nx_window_ensure_created(void) {
    if (!window_state.is_ready) nx_window_create();
}

static VALUE call_yield(VALUE arg) {
    return rb_yield(Qnil);
}

// 毎フレーム呼ばれる処理 (ナノ秒精度での堅牢な実装に戻した)
bool nx_window_tick(void) {
    Uint64 current_ns = get_ticks_ns();
    nx_window_update_fps(current_ns);

    int state = 0; 

    if (window_state.target_fps > 0) {
        Uint64 target_ns = 1000000000ULL / window_state.target_fps;
        Uint64 frame_time = current_ns - window_state.last_ns;
        window_state.last_ns = current_ns;

        // ラグの上限キャップ（処理落ち時の暴走ストッパー: 0.1秒 = 100ms）
        if (frame_time > 100000000ULL) frame_time = 100000000ULL; 

        window_state.lag_ns += frame_time;

        while (window_state.lag_ns >= target_ns) {
            nx_input_update();
            
            window_state.logic_count++;
            nx_window_clear_queue();
            
            rb_protect(call_yield, Qnil, &state);
            if (state != 0) rb_jump_tag(state);

            window_state.lag_ns -= target_ns;
        }
    } else {
        window_state.last_ns = current_ns;
        window_state.lag_ns = 0;
        nx_input_update();

        window_state.logic_count++;
        nx_window_clear_queue();

        rb_protect(call_yield, Qnil, &state);
        if (state != 0) rb_jump_tag(state);
    }
    
    SDL_SetRenderDrawColor(window_state.renderer, window_state.bgcolor.r, window_state.bgcolor.g, window_state.bgcolor.b, 255);
    SDL_RenderClear(window_state.renderer);
    nx_window_draw_queue();
    SDL_RenderPresent(window_state.renderer);
    window_state.render_count++;

    return true; 
}

static void nx_window_update_fps(Uint64 current_ns) {
    if (current_ns - window_state.fps_timer_ns >= 1000000000ULL) {
        window_state.logic_fps    = window_state.logic_count;
        window_state.render_fps   = window_state.render_count;
        window_state.logic_count  = 0;
        window_state.render_count = 0;
        window_state.fps_timer_ns = current_ns;
    }
}

// ウィンドウ拡大時やDPI環境でマウス座標がズレないよう強制補正する関数
void nx_window_convert_event(SDL_Event *event) {
    if (!window_state.renderer) return;

    float lx, ly;
    if (event->type == SDL_MOUSEMOTION) {
        SDL_RenderWindowToLogical(window_state.renderer, event->motion.x, event->motion.y, &lx, &ly);
        event->motion.x = (Sint32)lx;
        event->motion.y = (Sint32)ly;
    } else if (event->type == SDL_MOUSEBUTTONDOWN || event->type == SDL_MOUSEBUTTONUP) {
        SDL_RenderWindowToLogical(window_state.renderer, event->button.x, event->button.y, &lx, &ly);
        event->button.x = (Sint32)lx;
        event->button.y = (Sint32)ly;
    }
}

void nx_window_set_mouse_pos(float x, float y) {
    if (!window_state.window || !window_state.renderer) return;
    int window_x, window_y;
    SDL_RenderLogicalToWindow(window_state.renderer, x, y, &window_x, &window_y);
    SDL_WarpMouseInWindow(window_state.window, window_x, window_y);
}

SDL_Renderer* nx_window_get_renderer(void) {
    nx_window_ensure_created();
    return window_state.renderer;
}

// ================================================================================
// [3] 内部描画ロジック
// ================================================================================

static void nx_window_clear_queue(void) {
    for (int i = 0; i < draw_cmd_count; i++) {
        if (draw_queue[i].type == CMD_TEXT && draw_queue[i].data.text.texture) {
            SDL_DestroyTexture(draw_queue[i].data.text.texture);
            draw_queue[i].data.text.texture = NULL;
        }
    }
    draw_cmd_count = 0;
}

static bool nx_window_push_cmd(DrawCmd cmd) {
    if (draw_cmd_count < MAX_DRAW_CMDS) {
        cmd.order = draw_cmd_count;
        draw_queue[draw_cmd_count++] = cmd;
        return true;
    } else {
        static bool warned = false;
        if (!warned) {
            SDL_Log("NXRuby Warning: Draw command queue overflow! Max %d", MAX_DRAW_CMDS);
            warned = true;
        }
        return false;
    }
}

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
        SDL_RenderDrawPointF(renderer, cx + x, cy + y);
        if (x != 0) SDL_RenderDrawPointF(renderer, cx - x, cy + y);
        if (y != 0) SDL_RenderDrawPointF(renderer, cx + x, cy - y);
        if (x != 0 && y != 0) SDL_RenderDrawPointF(renderer, cx - x, cy - y);
        if (x != y) {
            SDL_RenderDrawPointF(renderer, cx + y, cy + x);
            if (y != 0) SDL_RenderDrawPointF(renderer, cx - y, cy + x);
            if (x != 0) SDL_RenderDrawPointF(renderer, cx + y, cy - x);
            if (x != 0 && y != 0) SDL_RenderDrawPointF(renderer, cx - y, cy - x);
        }
        y += 1;
        if (err <= 0) err += 2 * y + 1;
        else { x -= 1; err += 2 * (y - x) + 1; }
    }
}

static void nx_window_render_circle_fill(SDL_Renderer *renderer, float cx, float cy, float cr) {
    int x = (int)cr, y = 0, err = 0, last_x = -1;
    while (x >= y) {
        SDL_RenderDrawLineF(renderer, cx - x, cy + y, cx + x, cy + y);
        if (y != 0) SDL_RenderDrawLineF(renderer, cx - x, cy - y, cx + x, cy - y);
        if (x != last_x && x != y) {
            SDL_RenderDrawLineF(renderer, cx - y, cy + x, cx + y, cy + x);
            if (x != 0) SDL_RenderDrawLineF(renderer, cx - y, cy - x, cx + y, cy - x);
            last_x = x;
        }
        y += 1;
        if (err <= 0) err += 2 * y + 1;
        else { x -= 1; err += 2 * (y - x) + 1; }
    }
}

static void nx_window_draw_queue(void) {
    qsort(draw_queue, draw_cmd_count, sizeof(DrawCmd), nx_window_sort_z);

    for (int i = 0; i < draw_cmd_count; i++) {
        DrawCmd *cmd = &draw_queue[i];
        SDL_SetRenderDrawColor(window_state.renderer, cmd->color.r, cmd->color.g, cmd->color.b, cmd->color.a);

        switch (cmd->type) {
            case CMD_PIXEL: {
                SDL_RenderDrawPointF(window_state.renderer, cmd->data.pixel.x, cmd->data.pixel.y);
                break;
            }
            case CMD_LINE: {
                SDL_RenderDrawLineF(window_state.renderer, cmd->data.line.x1, cmd->data.line.y1, cmd->data.line.x2, cmd->data.line.y2);
                break;
            }
            case CMD_RECT: {
                SDL_FRect rect = {cmd->data.rect.x, cmd->data.rect.y, cmd->data.rect.w, cmd->data.rect.h};
                SDL_RenderDrawRectF(window_state.renderer, &rect);
                break;
            }
            case CMD_RECT_FILL: {
                SDL_FRect rect = {cmd->data.rect.x, cmd->data.rect.y, cmd->data.rect.w, cmd->data.rect.h};
                SDL_RenderFillRectF(window_state.renderer, &rect);
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
                    SDL_FRect dst_rect = {
                        cmd->data.image.x, cmd->data.image.y, 
                        cmd->data.image.src_rect.w, cmd->data.image.src_rect.h
                    };
                    SDL_RenderCopyF(window_state.renderer, cmd->data.image.texture, &cmd->data.image.src_rect, &dst_rect);
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

                    SDL_RenderCopyExF(window_state.renderer, cmd->data.image_ex.texture, &cmd->data.image_ex.src_rect, &dst_rect, cmd->data.image_ex.angle, &center, SDL_FLIP_NONE);

                    SDL_SetTextureAlphaMod(cmd->data.image_ex.texture, 255);
                    SDL_SetTextureBlendMode(cmd->data.image_ex.texture, SDL_BLENDMODE_BLEND);
                }
                break;
            }
            case CMD_TEXT: {
                if (cmd->data.text.texture) {
                    int w, h;
                    SDL_QueryTexture(cmd->data.text.texture, NULL, NULL, &w, &h);
                    SDL_FRect dst_rect = { cmd->data.text.x, cmd->data.text.y, (float)w, (float)h };
                    SDL_RenderCopyF(window_state.renderer, cmd->data.text.texture, NULL, &dst_rect);
                }
                break;
            }
            default: break;
        }
    }
}

static SDL_Color nx_window_parse_color(VALUE color) {
    Check_Type(color, T_ARRAY);
    long len = RARRAY_LEN(color);
    if (len < 3) rb_raise(rb_eArgError, "Color must have at least 3 elements (R, G, B)");

    int r = NUM2INT(rb_ary_entry(color, 0));
    int g = NUM2INT(rb_ary_entry(color, 1));
    int b = NUM2INT(rb_ary_entry(color, 2));
    int a = (len >= 4) ? NUM2INT(rb_ary_entry(color, 3)) : 255;

    #define CLAMP_255(v) ((Uint8)((v) < 0 ? 0 : ((v) > 255 ? 255 : (v))))
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

static VALUE nx_window_loop(VALUE self) {
    if (!rb_block_given_p()) rb_raise(rb_eArgError, "Window.loop requires a block (do...end)");
    
    nx_window_ensure_created();
    SDL_ShowWindow(window_state.window);

    SDL_Event event;
    bool running = true;

    while (running) {
        while (SDL_PollEvent(&event)) {
            nx_window_convert_event(&event);
            nx_input_handle_event(&event); 
            if (event.type == SDL_QUIT) {
                running = false;
            }
        }

        if (!nx_window_tick()) running = false;
        rb_thread_check_ints();
    }

    return Qnil;
}

static VALUE nx_window_close(VALUE self) {
    SDL_Event event;
    SDL_zero(&event);
    event.type = SDL_QUIT;
    SDL_PushEvent(&event);
    return Qnil;
}

static VALUE nx_window_logic_fps(VALUE self)  { return INT2NUM(window_state.logic_fps); }
static VALUE nx_window_render_fps(VALUE self) { return INT2NUM(window_state.render_fps); }
static VALUE nx_window_fps(VALUE self)        { return INT2NUM(window_state.target_fps); }

static VALUE nx_window_set_fps(VALUE self, VALUE rb_fps) {
    int fps = NUM2INT(rb_fps);
    window_state.target_fps = (fps < 0) ? 0 : fps;
    return rb_fps;
}

static VALUE nx_window_caption(VALUE self) {
    return rb_str_new_cstr(window_state.caption);
}

static VALUE nx_window_set_caption(VALUE self, VALUE rb_str) {
    const char *str = StringValueCStr(rb_str);
    snprintf(window_state.caption, sizeof(window_state.caption), "%s", str);
    if (window_state.window) SDL_SetWindowTitle(window_state.window, window_state.caption);
    return rb_str;
}

static VALUE nx_window_width(VALUE self) { return INT2NUM(window_state.width); }

static VALUE nx_window_set_width(VALUE self, VALUE rb_w) {
    int w = NUM2INT(rb_w);
    if (w < 1) w = 1;
    window_state.width = w;
    if (window_state.window) {
        SDL_SetWindowSize(window_state.window, (int)(window_state.width * window_state.scale), (int)(window_state.height * window_state.scale));
        SDL_RenderSetLogicalSize(window_state.renderer, window_state.width, window_state.height);
    }
    return rb_w;
}

static VALUE nx_window_height(VALUE self) { return INT2NUM(window_state.height); }

static VALUE nx_window_set_height(VALUE self, VALUE rb_h) {
    int h = NUM2INT(rb_h);
    if (h < 1) h = 1;
    window_state.height = h;
    if (window_state.window) {
        SDL_SetWindowSize(window_state.window, (int)(window_state.width * window_state.scale), (int)(window_state.height * window_state.scale));
        SDL_RenderSetLogicalSize(window_state.renderer, window_state.width, window_state.height);
    }
    return rb_h;
}

static VALUE nx_window_x(VALUE self) {
    if (window_state.window) SDL_GetWindowPosition(window_state.window, &window_state.x, &window_state.y);
    return INT2NUM(window_state.x);
}

static VALUE nx_window_set_x(VALUE self, VALUE rb_x) {
    window_state.x = NUM2INT(rb_x);
    if (window_state.window) SDL_SetWindowPosition(window_state.window, window_state.x, window_state.y);
    return rb_x;
}

static VALUE nx_window_y(VALUE self) {
    if (window_state.window) SDL_GetWindowPosition(window_state.window, &window_state.x, &window_state.y);
    return INT2NUM(window_state.y);
}

static VALUE nx_window_set_y(VALUE self, VALUE rb_y) {
    window_state.y = NUM2INT(rb_y);
    if (window_state.window) SDL_SetWindowPosition(window_state.window, window_state.x, window_state.y);
    return rb_y;
}

static VALUE nx_window_full_screen(VALUE self) {
    return window_state.is_fullscreen ? Qtrue : Qfalse;
}

static VALUE nx_window_set_full_screen(VALUE self, VALUE rb_b) {
    window_state.is_fullscreen = RTEST(rb_b); 
    if (window_state.window) SDL_SetWindowFullscreen(window_state.window, window_state.is_fullscreen ? SDL_WINDOW_FULLSCREEN : 0);
    return rb_b;
}

static VALUE nx_window_windowed(VALUE self) {
    return !window_state.is_fullscreen ? Qtrue : Qfalse;
}

static VALUE nx_window_set_windowed(VALUE self, VALUE rb_b) {
    bool b = RTEST(rb_b);
    window_state.is_fullscreen = !b;
    if (window_state.window) SDL_SetWindowFullscreen(window_state.window, window_state.is_fullscreen ? SDL_WINDOW_FULLSCREEN : 0);
    return rb_b;
}

static VALUE nx_window_running_time(VALUE self) {
    return UINT2NUM(SDL_GetTicks());
}

static VALUE nx_window_scale(VALUE self) {
    return DBL2NUM(window_state.scale);
}

static VALUE nx_window_set_scale(VALUE self, VALUE rb_s) {
    float s = (float)NUM2DBL(rb_s);
    if (s < 0.1f) s = 0.1f;
    window_state.scale = s;
    if (window_state.window) {
        SDL_SetWindowSize(window_state.window, 
            (int)(window_state.width * window_state.scale), 
            (int)(window_state.height * window_state.scale));
    }
    return rb_s;
}

static VALUE nx_window_active(VALUE self) {
    if (!window_state.window) return Qfalse;
    Uint32 flags = SDL_GetWindowFlags(window_state.window);
    return (flags & SDL_WINDOW_INPUT_FOCUS) ? Qtrue : Qfalse;
}

static VALUE nx_window_ox(VALUE self) { return DBL2NUM(window_state.ox); }
static VALUE nx_window_set_ox(VALUE self, VALUE rb_ox) { window_state.ox = (float)NUM2DBL(rb_ox); return rb_ox; }
static VALUE nx_window_oy(VALUE self) { return DBL2NUM(window_state.oy); }
static VALUE nx_window_set_oy(VALUE self, VALUE rb_oy) { window_state.oy = (float)NUM2DBL(rb_oy); return rb_oy; }

static VALUE nx_window_bgcolor(VALUE self) {
    VALUE ary = rb_ary_new2(4); 
    rb_ary_store(ary, 0, INT2NUM(window_state.bgcolor.r));
    rb_ary_store(ary, 1, INT2NUM(window_state.bgcolor.g));
    rb_ary_store(ary, 2, INT2NUM(window_state.bgcolor.b));
    rb_ary_store(ary, 3, INT2NUM(window_state.bgcolor.a));
    return ary;
}

static VALUE nx_window_set_bgcolor(VALUE self, VALUE rb_color) {
    window_state.bgcolor = nx_window_parse_color(rb_color);
    window_state.bgcolor.a = 255;
    return rb_color;
}

static VALUE nx_window_min_filter(VALUE self) { return INT2NUM((int)window_state.min_filter); }
static VALUE nx_window_set_min_filter(VALUE self, VALUE rb_filter) { window_state.min_filter = (SDL_ScaleMode)NUM2INT(rb_filter); return rb_filter; }
static VALUE nx_window_mag_filter(VALUE self) { return INT2NUM((int)window_state.mag_filter); }
static VALUE nx_window_set_mag_filter(VALUE self, VALUE rb_filter) { window_state.mag_filter = (SDL_ScaleMode)NUM2INT(rb_filter); return rb_filter; }

void nx_window_draw_sprite_c(float x, float y, float z, VALUE image, float angle, float scale_x, float scale_y, float center_x, float center_y, bool cx_def, bool cy_def, int alpha, VALUE blend) {
    DrawCmd cmd;
    if (!nx_window_setup_ex_cmd(image, x, y, z, &cmd)) return;

    cmd.data.image_ex.angle = angle;
    cmd.data.image_ex.scale_x = scale_x;
    cmd.data.image_ex.scale_y = scale_y;
    
    if (cx_def) cmd.data.image_ex.center_x = center_x;
    if (cy_def) cmd.data.image_ex.center_y = center_y;
    
    cmd.data.image_ex.alpha = alpha;

    if (SYMBOL_P(blend)) {
        ID blend_id = SYM2ID(blend);
        if (blend_id == rb_intern("add")) cmd.data.image_ex.blend = BLEND_ADD;
        else if (blend_id == rb_intern("sub")) cmd.data.image_ex.blend = BLEND_SUB;
        else if (blend_id == rb_intern("mod")) cmd.data.image_ex.blend = BLEND_MOD;
        else cmd.data.image_ex.blend = BLEND_ALPHA; 
    }
    nx_window_push_cmd(cmd);
}

// ================================================================================
// [5] Ruby API: 描画コマンド
// ================================================================================

static VALUE nx_window_draw_pixel(int argc, VALUE *argv, VALUE self) {
    VALUE rx, ry, rcolor, rz;
    rb_scan_args(argc, argv, "31", &rx, &ry, &rcolor, &rz); 
    DrawCmd cmd;
    cmd.type  = CMD_PIXEL;
    cmd.z     = NIL_P(rz) ? 0.0f : (float)NUM2DBL(rz);
    cmd.color = nx_window_parse_color(rcolor);
    cmd.data.pixel.x = (float)NUM2DBL(rx) - window_state.ox;
    cmd.data.pixel.y = (float)NUM2DBL(ry) - window_state.oy;
    nx_window_push_cmd(cmd);
    return Qnil;
}

static VALUE nx_window_draw_line(int argc, VALUE *argv, VALUE self) {
    VALUE rx1, ry1, rx2, ry2, rcolor, rz;
    rb_scan_args(argc, argv, "51", &rx1, &ry1, &rx2, &ry2, &rcolor, &rz);
    DrawCmd cmd;
    cmd.type  = CMD_LINE;
    cmd.z     = NIL_P(rz) ? 0.0f : (float)NUM2DBL(rz);
    cmd.color = nx_window_parse_color(rcolor);
    cmd.data.line.x1 = (float)NUM2DBL(rx1) - window_state.ox;
    cmd.data.line.y1 = (float)NUM2DBL(ry1) - window_state.oy;
    cmd.data.line.x2 = (float)NUM2DBL(rx2) - window_state.ox;
    cmd.data.line.y2 = (float)NUM2DBL(ry2) - window_state.oy;
    nx_window_push_cmd(cmd);
    return Qnil;
}

static VALUE nx_window_draw_rect(int argc, VALUE *argv, VALUE self) {
    VALUE rx, ry, rw, rh, rcolor, rz;
    rb_scan_args(argc, argv, "51", &rx, &ry, &rw, &rh, &rcolor, &rz);
    DrawCmd cmd;
    cmd.type  = CMD_RECT;
    cmd.z     = NIL_P(rz) ? 0.0f : (float)NUM2DBL(rz);
    cmd.color = nx_window_parse_color(rcolor);
    cmd.data.rect.x = (float)NUM2DBL(rx) - window_state.ox;
    cmd.data.rect.y = (float)NUM2DBL(ry) - window_state.oy;
    cmd.data.rect.w = (float)NUM2DBL(rw);
    cmd.data.rect.h = (float)NUM2DBL(rh);
    nx_window_push_cmd(cmd);
    return Qnil;
}

static VALUE nx_window_draw_rect_fill(int argc, VALUE *argv, VALUE self) {
    VALUE rx, ry, rw, rh, rcolor, rz;
    rb_scan_args(argc, argv, "51", &rx, &ry, &rw, &rh, &rcolor, &rz);
    DrawCmd cmd;
    cmd.type = CMD_RECT_FILL;
    cmd.z = NIL_P(rz) ? 0.0f : (float)NUM2DBL(rz);
    cmd.color = nx_window_parse_color(rcolor);
    cmd.data.rect.x = (float)NUM2DBL(rx) - window_state.ox;
    cmd.data.rect.y = (float)NUM2DBL(ry) - window_state.oy;
    cmd.data.rect.w = (float)NUM2DBL(rw);
    cmd.data.rect.h = (float)NUM2DBL(rh);
    nx_window_push_cmd(cmd);
    return Qnil;
}

static VALUE nx_window_draw_circle(int argc, VALUE *argv, VALUE self) {
    VALUE rcx, rcy, rcr, rcolor, rz;
    rb_scan_args(argc, argv, "41", &rcx, &rcy, &rcr, &rcolor, &rz);
    float cr = (float)NUM2DBL(rcr);
    if (cr < 0) return Qnil;
    DrawCmd cmd;
    cmd.z     = NIL_P(rz) ? 0.0f : (float)NUM2DBL(rz);
    cmd.color = nx_window_parse_color(rcolor);
    if (cr == 0) {
        cmd.type = CMD_PIXEL;
        cmd.data.pixel.x = (float)NUM2DBL(rcx) - window_state.ox;
        cmd.data.pixel.y = (float)NUM2DBL(rcy) - window_state.oy;
    } else {
        cmd.type = CMD_CIRCLE;
        cmd.data.circle.cx = (float)NUM2DBL(rcx) - window_state.ox;
        cmd.data.circle.cy = (float)NUM2DBL(rcy) - window_state.oy;
        cmd.data.circle.cr = cr;
    }
    nx_window_push_cmd(cmd);
    return Qnil;
}

static VALUE nx_window_draw_circle_fill(int argc, VALUE *argv, VALUE self) {
    VALUE rcx, rcy, rcr, rcolor, rz;
    rb_scan_args(argc, argv, "41", &rcx, &rcy, &rcr, &rcolor, &rz);
    float cr = (float)NUM2DBL(rcr);
    if (cr < 0) return Qnil;
    DrawCmd cmd;
    cmd.z = NIL_P(rz) ? 0.0f : (float)NUM2DBL(rz);
    cmd.color = nx_window_parse_color(rcolor);
    if (cr == 0) {
        cmd.type = CMD_PIXEL;
        cmd.data.pixel.x = (float)NUM2DBL(rcx) - window_state.ox;
        cmd.data.pixel.y = (float)NUM2DBL(rcy) - window_state.oy;
    } else {
        cmd.type = CMD_CIRCLE_FILL;
        cmd.data.circle.cx = (float)NUM2DBL(rcx) - window_state.ox;
        cmd.data.circle.cy = (float)NUM2DBL(rcy) - window_state.oy;
        cmd.data.circle.cr = cr;
    }
    nx_window_push_cmd(cmd);
    return Qnil;
}

static VALUE nx_window_draw(int argc, VALUE *argv, VALUE self) {
    VALUE rx, ry, rimg, rz;
    rb_scan_args(argc, argv, "31", &rx, &ry, &rimg, &rz);

    NxImage *img_data = nx_image_get_data(rimg);
    if (!img_data || !img_data->shared_tex || !img_data->shared_tex->texture) {
        rb_raise(rb_eArgError, "Invalid Image object");
    }

    DrawCmd cmd;
    cmd.type  = CMD_IMAGE;
    cmd.z     = NIL_P(rz) ? 0.0f : (float)NUM2DBL(rz);
    cmd.color = (SDL_Color){255, 255, 255, 255}; 
    cmd.data.image.texture = img_data->shared_tex->texture;
    cmd.data.image.src_rect = img_data->src_rect;
    cmd.data.image.x = (float)NUM2DBL(rx) - window_state.ox;
    cmd.data.image.y = (float)NUM2DBL(ry) - window_state.oy;

    nx_window_push_cmd(cmd);
    return Qnil;
}

static bool nx_window_setup_ex_cmd(VALUE image_obj, float x, float y, float z, DrawCmd *cmd) {
    NxImage *img_data = nx_image_get_data(image_obj);
    if (!img_data || !img_data->shared_tex || !img_data->shared_tex->texture) return false;

    cmd->type = CMD_IMAGE_EX;
    cmd->z = z;
    cmd->data.image_ex.texture = img_data->shared_tex->texture;
    cmd->data.image_ex.src_rect = img_data->src_rect;
    cmd->data.image_ex.x = x - window_state.ox;
    cmd->data.image_ex.y = y - window_state.oy;
    cmd->data.image_ex.angle = 0.0f;
    cmd->data.image_ex.scale_x = 1.0f;
    cmd->data.image_ex.scale_y = 1.0f;
    cmd->data.image_ex.center_x = img_data->width / 2.0f;
    cmd->data.image_ex.center_y = img_data->height / 2.0f;
    cmd->data.image_ex.alpha = 255;
    cmd->data.image_ex.blend = BLEND_ALPHA;
    return true;
}

static VALUE nx_window_draw_ex(int argc, VALUE *argv, VALUE self) {
    VALUE rx, ry, rimg, ropts;
    rb_scan_args(argc, argv, "31", &rx, &ry, &rimg, &ropts);

    DrawCmd cmd;
    if (!nx_window_setup_ex_cmd(rimg, (float)NUM2DBL(rx), (float)NUM2DBL(ry), 0.0f, &cmd)) return Qnil;

    if (!NIL_P(ropts)) {
        Check_Type(ropts, T_HASH);
        VALUE v;
        v = rb_hash_aref(ropts, ID2SYM(rb_intern("z")));        if (!NIL_P(v)) cmd.z = (float)NUM2DBL(v);
        v = rb_hash_aref(ropts, ID2SYM(rb_intern("angle")));    if (!NIL_P(v)) cmd.data.image_ex.angle = (float)NUM2DBL(v);
        v = rb_hash_aref(ropts, ID2SYM(rb_intern("scale_x")));  if (!NIL_P(v)) cmd.data.image_ex.scale_x = (float)NUM2DBL(v);
        v = rb_hash_aref(ropts, ID2SYM(rb_intern("scale_y")));  if (!NIL_P(v)) cmd.data.image_ex.scale_y = (float)NUM2DBL(v);
        v = rb_hash_aref(ropts, ID2SYM(rb_intern("center_x"))); if (!NIL_P(v)) cmd.data.image_ex.center_x = (float)NUM2DBL(v);
        v = rb_hash_aref(ropts, ID2SYM(rb_intern("center_y"))); if (!NIL_P(v)) cmd.data.image_ex.center_y = (float)NUM2DBL(v);
        v = rb_hash_aref(ropts, ID2SYM(rb_intern("alpha")));    if (!NIL_P(v)) cmd.data.image_ex.alpha = NUM2INT(v);
        v = rb_hash_aref(ropts, ID2SYM(rb_intern("blend")));
        if (SYMBOL_P(v)) {
            ID id = SYM2ID(v);
            if (id == rb_intern("add")) cmd.data.image_ex.blend = BLEND_ADD;
            else if (id == rb_intern("sub")) cmd.data.image_ex.blend = BLEND_SUB;
            else if (id == rb_intern("mod")) cmd.data.image_ex.blend = BLEND_MOD;
        }
    }
    nx_window_push_cmd(cmd);
    return Qnil;
}

static VALUE nx_window_draw_rot(int argc, VALUE *argv, VALUE self) {
    VALUE rx, ry, rimg, rangle, rcx, rcy, rz;
    rb_scan_args(argc, argv, "43", &rx, &ry, &rimg, &rangle, &rcx, &rcy, &rz);
    DrawCmd cmd;
    float z = NIL_P(rz) ? 0.0f : (float)NUM2DBL(rz);
    if (nx_window_setup_ex_cmd(rimg, (float)NUM2DBL(rx), (float)NUM2DBL(ry), z, &cmd)) {
        cmd.data.image_ex.angle = (float)NUM2DBL(rangle);
        if (!NIL_P(rcx)) cmd.data.image_ex.center_x = (float)NUM2DBL(rcx);
        if (!NIL_P(rcy)) cmd.data.image_ex.center_y = (float)NUM2DBL(rcy);
        nx_window_push_cmd(cmd);
    }
    return Qnil;
}

static VALUE nx_window_draw_scale(int argc, VALUE *argv, VALUE self) {
    VALUE rx, ry, rimg, rsx, rsy, rcx, rcy, rz;
    rb_scan_args(argc, argv, "53", &rx, &ry, &rimg, &rsx, &rsy, &rcx, &rcy, &rz);
    DrawCmd cmd;
    float z = NIL_P(rz) ? 0.0f : (float)NUM2DBL(rz);
    if (nx_window_setup_ex_cmd(rimg, (float)NUM2DBL(rx), (float)NUM2DBL(ry), z, &cmd)) {
        cmd.data.image_ex.scale_x = (float)NUM2DBL(rsx);
        cmd.data.image_ex.scale_y = (float)NUM2DBL(rsy);
        if (!NIL_P(rcx)) cmd.data.image_ex.center_x = (float)NUM2DBL(rcx);
        if (!NIL_P(rcy)) cmd.data.image_ex.center_y = (float)NUM2DBL(rcy);
        nx_window_push_cmd(cmd);
    }
    return Qnil;
}

static VALUE nx_window_draw_alpha(int argc, VALUE *argv, VALUE self) {
    VALUE rx, ry, rimg, ralpha, rz;
    rb_scan_args(argc, argv, "41", &rx, &ry, &rimg, &ralpha, &rz);
    DrawCmd cmd;
    float z = NIL_P(rz) ? 0.0f : (float)NUM2DBL(rz);
    if (nx_window_setup_ex_cmd(rimg, (float)NUM2DBL(rx), (float)NUM2DBL(ry), z, &cmd)) {
        cmd.data.image_ex.alpha = NUM2INT(ralpha);
        nx_window_push_cmd(cmd);
    }
    return Qnil;
}

static VALUE nx_window_draw_blend(int argc, VALUE *argv, NxBlendMode mode) {
    VALUE rx, ry, rimg, rz;
    rb_scan_args(argc, argv, "31", &rx, &ry, &rimg, &rz);
    DrawCmd cmd;
    float z = NIL_P(rz) ? 0.0f : (float)NUM2DBL(rz);
    if (nx_window_setup_ex_cmd(rimg, (float)NUM2DBL(rx), (float)NUM2DBL(ry), z, &cmd)) {
        cmd.data.image_ex.blend = mode;
        nx_window_push_cmd(cmd);
    }
    return Qnil;
}
static VALUE nx_window_draw_add(int argc, VALUE *argv, VALUE self) { return nx_window_draw_blend(argc, argv, BLEND_ADD); }
static VALUE nx_window_draw_sub(int argc, VALUE *argv, VALUE self) { return nx_window_draw_blend(argc, argv, BLEND_SUB); }
static VALUE nx_window_draw_mod(int argc, VALUE *argv, VALUE self) { return nx_window_draw_blend(argc, argv, BLEND_MOD); }

static VALUE nx_window_draw_tile(int argc, VALUE *argv, VALUE self) {
    VALUE rx, ry, rmap, rimgs, rxs, rys, rxc, ryc, rz;
    rb_scan_args(argc, argv, "45", &rx, &ry, &rmap, &rimgs, &rxs, &rys, &rxc, &ryc, &rz);

    int x_start = NIL_P(rxs) ? 0 : NUM2INT(rxs);
    int y_start = NIL_P(rys) ? 0 : NUM2INT(rys);
    float z     = NIL_P(rz)  ? 0.0f : (float)NUM2DBL(rz);

    long img_len = RARRAY_LEN(rimgs);
    if (img_len == 0) return Qnil;

    NxImage *first_img = nx_image_get_data(rb_ary_entry(rimgs, 0));
    if (!first_img) return Qnil;
    float tw = first_img->width;
    float th = first_img->height;

    long map_h = RARRAY_LEN(rmap);
    int y_count = NIL_P(ryc) ? (int)map_h - y_start : NUM2INT(ryc);
    
    for (int iy = 0; iy < y_count; iy++) {
        int my = y_start + iy;
        if (my < 0 || my >= map_h) continue;
        VALUE row = rb_ary_entry(rmap, my);
        if (!RB_TYPE_P(row, T_ARRAY)) continue;

        long row_len = RARRAY_LEN(row);
        int x_count = NIL_P(rxc) ? (int)row_len - x_start : NUM2INT(rxc);

        for (int ix = 0; ix < x_count; ix++) {
            int mx = x_start + ix;
            if (mx < 0 || mx >= row_len) continue;
            VALUE idx_v = rb_ary_entry(row, mx);
            if (NIL_P(idx_v)) continue;

            int idx = NUM2INT(idx_v);
            if (idx < 0 || idx >= img_len) continue;

            NxImage *img = nx_image_get_data(rb_ary_entry(rimgs, idx));
            if (!img || !img->shared_tex->texture) continue;

            float dx = (float)NUM2DBL(rx) + (ix * tw) - window_state.ox;
            float dy = (float)NUM2DBL(ry) + (iy * th) - window_state.oy;
            if (dx + tw < 0 || dx > window_state.width || dy + th < 0 || dy > window_state.height) continue;

            DrawCmd cmd;
            cmd.type = CMD_IMAGE; cmd.z = z; cmd.color = (SDL_Color){255, 255, 255, 255};
            cmd.data.image.texture = img->shared_tex->texture;
            cmd.data.image.src_rect = img->src_rect;
            cmd.data.image.x = dx; cmd.data.image.y = dy;
            nx_window_push_cmd(cmd);
        }
    }
    return Qnil;
}

static VALUE nx_window_draw_font(int argc, VALUE *argv, VALUE self) {
    VALUE rx, ry, rtext, rfont, ropts;
    rb_scan_args(argc, argv, "41", &rx, &ry, &rtext, &rfont, &ropts);

    TTF_Font *ttf = nx_font_get_ptr(rfont);
    if (!ttf) rb_raise(rb_eArgError, "Invalid Font object");

    DrawCmd cmd;
    cmd.type = CMD_TEXT; cmd.z = 0.0f; cmd.color = (SDL_Color){255, 255, 255, 255};

    if (!NIL_P(ropts)) {
        VALUE v;
        v = rb_hash_aref(ropts, ID2SYM(rb_intern("z")));     if (!NIL_P(v)) cmd.z = (float)NUM2DBL(v);
        v = rb_hash_aref(ropts, ID2SYM(rb_intern("color"))); if (!NIL_P(v)) cmd.color = nx_window_parse_color(v);
    }

    SDL_Surface *surf = TTF_RenderUTF8_Blended(ttf, StringValueCStr(rtext), cmd.color);
    if (!surf) return Qnil;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(window_state.renderer, surf);
    SDL_DestroySurface(surf);
    if (!tex) return Qnil;

    cmd.data.text.texture = tex;
    cmd.data.text.x = (float)NUM2DBL(rx) - window_state.ox;
    cmd.data.text.y = (float)NUM2DBL(ry) - window_state.oy;

    if (!nx_window_push_cmd(cmd)) SDL_DestroyTexture(tex);
    return Qnil;
}

// ================================================================================
// [6] 初期化とメソッド登録
// ================================================================================

void nx_window_init(void) {
    VALUE mWindow = rb_define_module("Window");

    rb_define_const(rb_cObject, "TEXF_POINT",  INT2NUM(SDL_SCALEMODE_NEAREST));
    rb_define_const(rb_cObject, "TEXF_LINEAR", INT2NUM(SDL_SCALEMODE_LINEAR));

    rb_define_module_function(mWindow, "loop",             nx_window_loop,              0);
    rb_define_module_function(mWindow, "close",            nx_window_close,             0);
    rb_define_module_function(mWindow, "logic_fps",        nx_window_logic_fps,         0);
    rb_define_module_function(mWindow, "render_fps",       nx_window_render_fps,        0);
    rb_define_module_function(mWindow, "fps",              nx_window_fps,               0);
    rb_define_module_function(mWindow, "fps=",             nx_window_set_fps,           1);
    rb_define_module_function(mWindow, "caption",          nx_window_caption,           0);
    rb_define_module_function(mWindow, "caption=",         nx_window_set_caption,       1);
    rb_define_module_function(mWindow, "width",            nx_window_width,             0);
    rb_define_module_function(mWindow, "width=",           nx_window_set_width,         1);
    rb_define_module_function(mWindow, "height",           nx_window_height,            0);
    rb_define_module_function(mWindow, "height=",          nx_window_set_height,        1);

    rb_define_module_function(mWindow, "x",                nx_window_x,                 0);
    rb_define_module_function(mWindow, "x=",               nx_window_set_x,             1);
    rb_define_module_function(mWindow, "y",                nx_window_y,                 0);
    rb_define_module_function(mWindow, "y=",               nx_window_set_y,             1);
    rb_define_module_function(mWindow, "full_screen?",     nx_window_full_screen,       0);
    rb_define_module_function(mWindow, "full_screen=",     nx_window_set_full_screen,   1);
    rb_define_module_function(mWindow, "windowed?",        nx_window_windowed,          0);
    rb_define_module_function(mWindow, "windowed=",        nx_window_set_windowed,      1);
    rb_define_module_function(mWindow, "running_time",     nx_window_running_time,      0);
    rb_define_module_function(mWindow, "scale",            nx_window_scale,             0);
    rb_define_module_function(mWindow, "scale=",           nx_window_set_scale,         1);
    rb_define_module_function(mWindow, "active?",          nx_window_active,            0);
    rb_define_module_function(mWindow, "ox",               nx_window_ox,                0);
    rb_define_module_function(mWindow, "ox=",              nx_window_set_ox,            1);
    rb_define_module_function(mWindow, "oy",               nx_window_oy,                0);
    rb_define_module_function(mWindow, "oy=",              nx_window_set_oy,            1);
    rb_define_module_function(mWindow, "bgcolor",          nx_window_bgcolor,           0);
    rb_define_module_function(mWindow, "bgcolor=",         nx_window_set_bgcolor,       1);
    rb_define_module_function(mWindow, "min_filter",       nx_window_min_filter,        0);
    rb_define_module_function(mWindow, "min_filter=",      nx_window_set_min_filter,    1);
    rb_define_module_function(mWindow, "mag_filter",       nx_window_mag_filter,        0);
    rb_define_module_function(mWindow, "mag_filter=",      nx_window_set_mag_filter,    1);

    rb_define_module_function(mWindow, "draw_pixel",       nx_window_draw_pixel,       -1);
    rb_define_module_function(mWindow, "draw_line",        nx_window_draw_line,        -1);
    rb_define_module_function(mWindow, "draw_rect",        nx_window_draw_rect,        -1);
    rb_define_module_function(mWindow, "draw_rect_fill",   nx_window_draw_rect_fill,   -1);
    rb_define_module_function(mWindow, "draw_circle",      nx_window_draw_circle,      -1);
    rb_define_module_function(mWindow, "draw_circle_fill", nx_window_draw_circle_fill, -1);
    rb_define_module_function(mWindow, "draw",             nx_window_draw,             -1);
    rb_define_module_function(mWindow, "draw_ex",          nx_window_draw_ex,          -1);
    rb_define_module_function(mWindow, "draw_rot",         nx_window_draw_rot,         -1);
    rb_define_module_function(mWindow, "draw_scale",       nx_window_draw_scale,       -1);
    rb_define_module_function(mWindow, "draw_alpha",       nx_window_draw_alpha,       -1);
    rb_define_module_function(mWindow, "draw_add",         nx_window_draw_add,         -1);
    rb_define_module_function(mWindow, "draw_sub",         nx_window_draw_sub,         -1);
    rb_define_module_function(mWindow, "draw_mod",         nx_window_draw_mod,         -1);
    rb_define_module_function(mWindow, "draw_tile",        nx_window_draw_tile,        -1);
    rb_define_module_function(mWindow, "draw_font",        nx_window_draw_font,        -1);
}

void nx_window_cleanup(void) {
    nx_window_clear_queue();
    
    if (window_state.renderer) {
        SDL_DestroyRenderer(window_state.renderer);
        window_state.renderer = NULL;
    }
    if (window_state.window) {
        SDL_DestroyWindow(window_state.window);
        window_state.window = NULL;
    }
    window_state.is_ready = false;
}