#include <mruby.h>
#include <mruby/data.h>
#include <mruby/class.h>
#include <mruby/error.h>
#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>
#include "sound.h"

static MIX_Mixer *global_mixer = NULL;

// ================================================================================
// [1] メモリ管理
// ================================================================================
static void nx_sound_free(mrb_state *mrb, void *ptr) {
    if (!ptr) return;
    NxSound *snd = (NxSound*)ptr;
    
    // トラックを先に破棄してからオーディオを破棄する
    if (snd->track) MIX_DestroyTrack(snd->track);
    if (snd->audio) MIX_DestroyAudio(snd->audio);
    
    mrb_free(mrb, snd);
}

static const struct mrb_data_type nx_sound_type = { "Sound", nx_sound_free };

// ================================================================================
// [2] Ruby API: Soundクラス
// ================================================================================

// --- Sound.new("assets/bgm.mp3") ---
static mrb_value nx_sound_initialize(mrb_state *mrb, mrb_value self) {
    char *path;
    mrb_get_args(mrb, "z", &path);

    if (!global_mixer) mrb_raise(mrb, E_RUNTIME_ERROR, "Audio mixer is not initialized.");

    MIX_Audio *audio = MIX_LoadAudio(global_mixer, path, true);
    if (!audio) mrb_raisef(mrb, E_RUNTIME_ERROR, "Failed to load sound '%s': %s", path, SDL_GetError());

    // ★自分専用のトラック（再生レーン）を作成し、オーディオをセットする
    MIX_Track *track = MIX_CreateTrack(global_mixer);
    MIX_SetTrackAudio(track, audio);

    NxSound *snd = mrb_malloc(mrb, sizeof(NxSound));
    snd->audio = audio;
    snd->track = track;
    snd->loop_count = 0; // デフォルトは 0（＝ループせず1回だけ再生）

    DATA_PTR(self) = snd;
    DATA_TYPE(self) = &nx_sound_type;

    return self;
}

// --- Sound#play ---
static mrb_value nx_sound_play(mrb_state *mrb, mrb_value self) {
    NxSound *snd = (NxSound*)mrb_data_get_ptr(mrb, self, &nx_sound_type);
    if (!snd || !snd->track) return mrb_nil_value();

    // ★プロパティを作ってループ回数を指定して再生！
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, snd->loop_count);
    MIX_PlayTrack(snd->track, props);
    SDL_DestroyProperties(props);
    
    return mrb_nil_value();
}

// --- Sound#stop ---
static mrb_value nx_sound_stop(mrb_state *mrb, mrb_value self) {
    NxSound *snd = (NxSound*)mrb_data_get_ptr(mrb, self, &nx_sound_type);
    if (snd && snd->track) {
        // 第2引数の0は「フェードアウトせず即座に止める」の意味
        MIX_StopTrack(snd->track, 0); 
    }
    return mrb_nil_value();
}

// --- Sound#set_volume(255) ---
static mrb_value nx_sound_set_volume(mrb_state *mrb, mrb_value self) {
    mrb_int vol;
    mrb_get_args(mrb, "i", &vol);
    
    NxSound *snd = (NxSound*)mrb_data_get_ptr(mrb, self, &nx_sound_type);
    if (snd && snd->track) {
        if (vol < 0) vol = 0;
        if (vol > 255) vol = 255;
        // DXRubyの 0〜255 を、SDL3_mixerの 0.0f〜1.0f に変換
        MIX_SetTrackGain(snd->track, (float)vol / 255.0f);
    }
    return self; // メソッドチェーン用にselfを返す
}

// --- Sound#loop_count= ---
static mrb_value nx_sound_set_loop_count(mrb_state *mrb, mrb_value self) {
    mrb_int count;
    mrb_get_args(mrb, "i", &count);
    
    NxSound *snd = (NxSound*)mrb_data_get_ptr(mrb, self, &nx_sound_type);
    if (snd) {
        // DXRubyでは無限ループさせる時に色々書き方がありますが、今回は -1 で無限ループとします
        snd->loop_count = (int)count;
    }
    return mrb_int_value(mrb, count);
}

// --- (おまけ) Sound#frequency= (ピッチ変更) ---
static mrb_value nx_sound_set_frequency(mrb_state *mrb, mrb_value self) {
    mrb_float ratio;
    // SDL3_mixerに合わせて、1.0を通常速度とする倍率(ratio)で指定します
    mrb_get_args(mrb, "f", &ratio);
    
    NxSound *snd = (NxSound*)mrb_data_get_ptr(mrb, self, &nx_sound_type);
    if (snd && snd->track) {
        MIX_SetTrackFrequencyRatio(snd->track, (float)ratio);
    }
    return mrb_float_value(mrb, ratio);
}


// --- Sound#dispose ---
static mrb_value nx_sound_dispose(mrb_state *mrb, mrb_value self) {
    NxSound *snd = (NxSound*)mrb_data_get_ptr(mrb, self, &nx_sound_type);
    if (snd) {
        nx_sound_free(mrb, snd);
        DATA_PTR(self) = NULL;
    }
    return mrb_nil_value();
}

static mrb_value nx_sound_is_disposed(mrb_state *mrb, mrb_value self) {
    return mrb_bool_value(DATA_PTR(self) == NULL);
}

// ================================================================================
// [3] 初期化とメソッド登録
// ================================================================================

void nx_sound_init(mrb_state *mrb) {
    MIX_Init();
    global_mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);

    struct RClass *Sound = mrb_define_class(mrb, "Sound", mrb->object_class);
    MRB_SET_INSTANCE_TT(Sound, MRB_TT_DATA);

    mrb_define_method(mrb, Sound, "initialize",   nx_sound_initialize,     MRB_ARGS_REQ(1));
    mrb_define_method(mrb, Sound, "play",         nx_sound_play,           MRB_ARGS_NONE());
    mrb_define_method(mrb, Sound, "stop",         nx_sound_stop,           MRB_ARGS_NONE());
    mrb_define_method(mrb, Sound, "set_volume",   nx_sound_set_volume,     MRB_ARGS_REQ(1));
    mrb_define_method(mrb, Sound, "loop_count=",  nx_sound_set_loop_count, MRB_ARGS_REQ(1));
    mrb_define_method(mrb, Sound, "frequency=",   nx_sound_set_frequency,  MRB_ARGS_REQ(1));
    
    mrb_define_method(mrb, Sound, "dispose",      nx_sound_dispose,        MRB_ARGS_NONE());
    mrb_define_method(mrb, Sound, "disposed?",    nx_sound_is_disposed,    MRB_ARGS_NONE());
}

void nx_sound_cleanup(void) {
    if (global_mixer) {
        MIX_DestroyMixer(global_mixer);
        global_mixer = NULL;
    }
    MIX_Quit();
}