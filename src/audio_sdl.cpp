// audio_sdl.cpp
#include "audio_sdl.hpp"
#include <iostream>
#include <fstream>
// #define DR_WAV_IMPLEMENTATION
#include <dr_wav.h>
// #define STB_VORBIS_IMPLEMENTATION
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

SdlAudioPlayer::SdlAudioPlayer() {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        std::cerr << "[Audio] SDL audio init failed: '" << SDL_GetError() << "'\n";
    }
}

SdlAudioPlayer::~SdlAudioPlayer() {
    stop();
    _terminate = true;
    if (_streamThread.joinable()) {
        _streamThread.join();
    }
    if (_device) {
        SDL_CloseAudioDevice(_device);
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

bool SdlAudioPlayer::load(const std::string& filepath) {
    if (filepath.ends_with(".wav")) {
        return loadWav(filepath);
    } else if (filepath.ends_with(".ogg")) {
        return loadOgg(filepath);
    } else {
        std::cerr << "[Audio] Unsupported file type: " << filepath << '\n';
        return false;
    }
}

bool SdlAudioPlayer::loadWav(const std::string& filepath) {
    drwav wav;
    if (!drwav_init_file(&wav, filepath.c_str(), nullptr)) {
        std::cerr << "[Audio] Failed to open WAV file: " << filepath << '\n';
        return false;
    }

    _spec.freq = wav.sampleRate;
    _spec.format = SDL_AUDIO_S16LE;
    _spec.channels = wav.channels;

    std::size_t totalSamples = wav.totalPCMFrameCount * wav.channels;
    _audioData.resize(totalSamples * sizeof(drwav_int16));
    drwav_read_pcm_frames_s16(&wav, wav.totalPCMFrameCount, reinterpret_cast<drwav_int16*>(_audioData.data()));

    drwav_uninit(&wav);

    return true;
}

bool SdlAudioPlayer::loadOgg(const std::string& filepath) {
    int error = 0;
    stb_vorbis* vorbis = stb_vorbis_open_filename(filepath.c_str(), &error, nullptr);
    if (!vorbis) {
        std::cerr << "[Audio] Failed to open OGG file: " << filepath << " (error " << error << ")\n";
        return false;
    }

    stb_vorbis_info info = stb_vorbis_get_info(vorbis);
    _spec.freq = info.sample_rate;
    _spec.format = SDL_AUDIO_S16LE;
    _spec.channels = info.channels;

    int samples = stb_vorbis_stream_length_in_samples(vorbis) * info.channels;
    _audioData.resize(samples * sizeof(short));

    stb_vorbis_get_samples_short_interleaved(vorbis, info.channels, reinterpret_cast<short*>(_audioData.data()), samples);

    stb_vorbis_close(vorbis);

    return true;
}

void SdlAudioPlayer::play(bool loop) {
    _loop = loop;
    _playing = true;
    _streamActive = true;
    _cursor = 0;

    if (!_device) {
        _device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &_spec);
        if (!_device) {
            std::cerr << "[Audio] Failed to open audio device: " << SDL_GetError() << '\n';
            return;
        }
    }

    if (!_stream) {
        _stream = SDL_CreateAudioStream(&_spec, &_spec); // source and target formats same
        if (!_stream) {
            std::cerr << "[Audio] Failed to create audio stream: " << SDL_GetError() << '\n';
            return;
        }
    }

    if (!SDL_BindAudioStream(_device, _stream)) {
        std::cerr << "[Audio] Failed to bind audio stream: " << SDL_GetError() << '\n';
        return;
    }

    SDL_ResumeAudioDevice(_device);

    if (!_streamThread.joinable()) {
        _streamThread = std::thread(&SdlAudioPlayer::streamLoop, this);
    }
}

void SdlAudioPlayer::stop() {
    _streamActive = false;
    _playing = false;
    if (_device) {
        SDL_PauseAudioDevice(_device);
    }
}


void SdlAudioPlayer::streamLoop() {
    while (!_terminate) {
        if (_streamActive && _device && _stream) {
            std::size_t remaining = _audioData.size() - _cursor;
            if (remaining > 0) {
                std::size_t toSend = std::min(_chunkSize, remaining);
                if (!SDL_PutAudioStreamData(_stream, _audioData.data() + _cursor, static_cast<Uint32>(toSend))) {
                    std::cerr << "[Audio] Failed to put audio data: " << SDL_GetError() << '\n';
                }
                _cursor += toSend;
            } else if (_loop) {
                _cursor = 0;
            } else {
                stop();
            }
        }

        SDL_Delay(10); // SDL3 still has SDL_Delay for millisecond sleep
    }
}

