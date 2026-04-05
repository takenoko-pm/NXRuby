#include <ruby.h>
#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#include "sound.h"

static MIX_Mixer *global_mixer = NULL;

// ================================================================================
// [1] メモリ管理
// ================================================================================

// GCから呼ばれるメモリ解放関数
static void nx_sound_free(void *ptr) {
    if (!ptr) return;
    NxSound *snd = (NxSound*)ptr;
    
    // トラックを先に破棄してからオーディオを破棄する
    if (snd->track) MIX_DestroyTrack(snd->track);
    if (snd->audio) MIX_DestroyAudio(snd->audio);
    
    ruby_xfree(snd);
}

// CRuby用の型定義
static const rb_data_type_t nx_sound_data_type = {
    "NXRuby::Sound",
    { NULL, nx_sound_free, NULL, }, // マーク関数, 解放関数, サイズ関数
    NULL, NULL,
    RUBY_TYPED_FREE_IMMEDIATELY
};

// ================================================================================
// [2] Ruby API: Soundクラスの実装
// ================================================================================

// --- Sound.new(path) ---
static VALUE nx_sound_initialize(VALUE self, VALUE rpath) {
    const char *path = StringValueCStr(rpath);

    if (!global_mixer) {
        rb_raise(rb_eRuntimeError, "Audio mixer is not initialized.");
    }

    MIX_Audio *audio = MIX_LoadAudio(global_mixer, path, true);
    if (!audio) {
        rb_raise(rb_eRuntimeError, "Failed to load sound '%s': %s", path, SDL_GetError());
    }

    // 自分専用のトラックを作成
    MIX_Track *track = MIX_CreateTrack(global_mixer);
    MIX_SetTrackAudio(track, audio);

    NxSound *snd = ALLOC(NxSound);
    snd->audio = audio;
    snd->track = track;
    snd->loop_count = 0;

    // Cの構造体を Rubyオブジェクトに紐付ける
    DATA_PTR(self) = snd;

    return self;
}

// --- Sound#play ---
static VALUE nx_sound_play(VALUE self) {
    NxSound *snd;
    TypedData_Get_Struct(self, NxSound, &nx_sound_data_type, snd);
    
    if (snd && snd->track) {
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, snd->loop_count);
        MIX_PlayTrack(snd->track, props);
        SDL_DestroyProperties(props);
    }
    return Qnil;
}

// --- Sound#stop ---
static VALUE nx_sound_stop(VALUE self) {
    NxSound *snd;
    TypedData_Get_Struct(self, NxSound, &nx_sound_data_type, snd);
    
    if (snd && snd->track) {
        MIX_StopTrack(snd->track, 0); 
    }
    return Qnil;
}

// --- Sound#set_volume(vol) ---
static VALUE nx_sound_set_volume(VALUE self, VALUE rvol) {
    int vol = NUM2INT(rvol);
    NxSound *snd;
    TypedData_Get_Struct(self, NxSound, &nx_sound_data_type, snd);
    
    if (snd && snd->track) {
        if (vol < 0) vol = 0;
        if (vol > 255) vol = 255;
        MIX_SetTrackGain(snd->track, (float)vol / 255.0f);
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

// --- Sound#frequency = ratio ---
static VALUE nx_sound_set_frequency(VALUE self, VALUE rratio) {
    float ratio = (float)NUM2DBL(rratio);
    NxSound *snd;
    TypedData_Get_Struct(self, NxSound, &nx_sound_data_type, snd);
    
    if (snd && snd->track) {
        MIX_SetTrackFrequencyRatio(snd->track, ratio);
    }
    return rratio;
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
    MIX_Init();
    global_mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);

    VALUE cSound = rb_define_class("Sound", rb_cObject);
    rb_define_alloc_func(cSound, nx_sound_alloc);

    rb_define_method(cSound, "initialize",  nx_sound_initialize, 1);
    rb_define_method(cSound, "play",        nx_sound_play, 0);
    rb_define_method(cSound, "stop",        nx_sound_stop, 0);
    rb_define_method(cSound, "set_volume",  nx_sound_set_volume, 1);
    rb_define_method(cSound, "loop_count=", nx_sound_set_loop_count, 1);
    rb_define_method(cSound, "frequency=",  nx_sound_set_frequency, 1);
    rb_define_method(cSound, "dispose",     nx_sound_dispose, 0);
    rb_define_method(cSound, "disposed?",   nx_sound_is_disposed, 0);
}

void nx_sound_cleanup(void) {
    if (global_mixer) {
        MIX_DestroyMixer(global_mixer);
        global_mixer = NULL;
    }
    MIX_Quit();
}