#include <ruby.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include "sound.h"

// ================================================================================
// [1] メモリ管理
// ================================================================================

// GCから呼ばれるメモリ解放関数
static void nx_sound_free(void *ptr) {
    if (!ptr) return;
    NxSound *snd = (NxSound*)ptr;
    
    if (snd->chunk) {
        // メモリ解放前に、もしこの音がどこかのチャンネルで鳴っていたら全て強制停止する
        int num_channels = Mix_AllocateChannels(-1);
        for (int i = 0; i < num_channels; i++) {
            if (Mix_GetChunk(i) == snd->chunk) {
                Mix_HaltChannel(i);
            }
        }
        Mix_FreeChunk(snd->chunk);
    }
    
    ruby_xfree(snd);
}

// CRuby用の型定義
static const rb_data_type_t nx_sound_data_type = {
    "NXRuby::Sound",
    { NULL, nx_sound_free, NULL, },
    NULL, NULL,
    RUBY_TYPED_FREE_IMMEDIATELY
};

// ================================================================================
// [2] Ruby API: Soundクラスの実装
// ================================================================================

// --- Sound.new(path) ---
static VALUE nx_sound_initialize(VALUE self, VALUE rpath) {
    const char *path = StringValueCStr(rpath);

    Mix_Chunk *chunk = Mix_LoadWAV(path);
    if (!chunk) {
        rb_raise(rb_eRuntimeError, "Failed to load sound '%s': %s", path, Mix_GetError());
    }

    NxSound *snd = ALLOC(NxSound);
    snd->chunk = chunk;
    snd->loop_count = 0;

    DATA_PTR(self) = snd;

    return self;
}

// --- Sound#play ---
static VALUE nx_sound_play(VALUE self) {
    NxSound *snd;
    TypedData_Get_Struct(self, NxSound, &nx_sound_data_type, snd);
    
    if (snd && snd->chunk) {
        // -1 (空きチャンネル) で再生。戻り値(チャンネル番号)は管理しなくてよい
        Mix_PlayChannel(-1, snd->chunk, snd->loop_count);
    }
    return Qnil;
}

// --- Sound#stop ---
static VALUE nx_sound_stop(VALUE self) {
    NxSound *snd;
    TypedData_Get_Struct(self, NxSound, &nx_sound_data_type, snd);
    
    if (snd && snd->chunk) {
        // 全チャンネルをスキャンし、自分の Chunk が鳴っているチャンネルだけを止める
        int num_channels = Mix_AllocateChannels(-1);
        for (int i = 0; i < num_channels; i++) {
            if (Mix_GetChunk(i) == snd->chunk) {
                Mix_HaltChannel(i);
            }
        }
    }
    return Qnil;
}

// --- Sound#set_volume(vol) ---
static VALUE nx_sound_set_volume(VALUE self, VALUE rvol) {
    int vol = NUM2INT(rvol);
    NxSound *snd;
    TypedData_Get_Struct(self, NxSound, &nx_sound_data_type, snd);
    
    if (snd && snd->chunk) {
        if (vol < 0) vol = 0;
        if (vol > 255) vol = 255;
        // 0-255 の指定を SDL2_mixer の 0-128 にマッピング
        Mix_VolumeChunk(snd->chunk, (vol * MIX_MAX_VOLUME) / 255);
    }
    return self;
}

// --- Sound#loop_count = count ---
static VALUE nx_sound_set_loop_count(VALUE self, VALUE rcount) {
    int count = NUM2INT(rcount);
    NxSound *snd;
    TypedData_Get_Struct(self, NxSound, &nx_sound_data_type, snd);
    
    if (snd) {
        snd->loop_count = count;
    }
    return rcount;
}

static VALUE nx_sound_dispose(VALUE self) {
    NxSound *snd;
    TypedData_Get_Struct(self, NxSound, &nx_sound_data_type, snd);
    if (snd) {
        nx_sound_free(snd);
        DATA_PTR(self) = NULL;
    }
    return Qnil;
}

static VALUE nx_sound_is_disposed(VALUE self) {
    return DATA_PTR(self) == NULL ? Qtrue : Qfalse;
}

static VALUE nx_sound_alloc(VALUE klass) {
    return TypedData_Wrap_Struct(klass, &nx_sound_data_type, NULL);
}

// ================================================================================
// [3] 初期化と終了処理
// ================================================================================

void nx_sound_init(void) {
    // SDL2_mixerの初期化
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        rb_raise(rb_eRuntimeError, "SDL_mixer could not initialize! Error: %s", Mix_GetError());
    }

    // チャンネル数を多めに確保しておく（デフォルトは8）
    Mix_AllocateChannels(32);

    VALUE cSound = rb_define_class("Sound", rb_cObject);
    rb_define_alloc_func(cSound, nx_sound_alloc);

    rb_define_method(cSound, "initialize",  nx_sound_initialize, 1);
    rb_define_method(cSound, "play",        nx_sound_play, 0);
    rb_define_method(cSound, "stop",        nx_sound_stop, 0);
    rb_define_method(cSound, "set_volume",  nx_sound_set_volume, 1);
    rb_define_method(cSound, "loop_count=", nx_sound_set_loop_count, 1);
    rb_define_method(cSound, "dispose",     nx_sound_dispose, 0);
    rb_define_method(cSound, "disposed?",   nx_sound_is_disposed, 0);
}

void nx_sound_cleanup(void) {
    Mix_CloseAudio();
}