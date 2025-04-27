#include "audio.hpp"

#include <fstream>
#include <print>
#include <format>
#include <span>

#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>

#include <EmscriptenHelper.hpp>

#define DR_WAV_IMPLEMENTATION
#include <dr_wav.h>
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c> // yes, you need .c

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>

bool em_visibilitychange_callback(int, const EmscriptenVisibilityChangeEvent* evt, void*) {
    constexpr int visibleFPS = 0; // 0 = requestAnimationFrame
    constexpr int hiddenFPS  = 5; // ~200ms refresh when hidden
    if (evt->hidden) {
        emscripten_set_main_loop_timing(EM_TIMING_SETTIMEOUT, 1000 / hiddenFPS);
        std::println("[MainLoop] Switched to setTimeout {}ms (hidden)", 1000 / hiddenFPS);
    } else {
        emscripten_set_main_loop_timing(EM_TIMING_RAF, visibleFPS);
        std::println("[MainLoop] Switched to requestAnimationFrame (visible)");
    }
    return true;
}
#endif

AudioPlayer::AudioPlayer() {
    std::println("[Audio] Initialising OpenAL...");

    _device = alcOpenDevice(nullptr);
    if (!_device) {
        std::println("[Audio] Failed to open audio device.");
        return;
    }
    std::string_view ext = alcGetString(_device, ALC_EXTENSIONS);
    std::println("[Audio] Supported extensions: {}", ext);

    _context = alcCreateContext(_device, nullptr);
    if (!_context || !alcMakeContextCurrent(_context)) {
        std::println("[Audio] Failed to create/make current OpenAL context.");
        if (_context) alcDestroyContext(_context);
        if (_device) alcCloseDevice(_device);
        return;
    }

    alGenSources(1, &_source);
    alGenBuffers(2, _buffers); // double-buffering

    _streamThread = std::thread(&AudioPlayer::streamLoop, this);

#ifdef __EMSCRIPTEN__
    emscripten_set_visibilitychange_callback(nullptr, false, em_visibilitychange_callback);
#endif
}

AudioPlayer::~AudioPlayer() {
    stop();
    _terminate = true;

    if (_streamThread.joinable()) {
        _streamThread.join();
    }

    alDeleteSources(1, &_source);
    alDeleteBuffers(2, _buffers);

    if (_context) {
        alcMakeContextCurrent(nullptr);
        alcDestroyContext(_context);
    }
    if (_device) {
        alcCloseDevice(_device);
    }
}

bool AudioPlayer::load(const std::string& filepath) {
    if (filepath.ends_with(".wav")) {
        return loadWav(filepath);
    } else if (filepath.ends_with(".ogg")) {
        return loadOgg(filepath);
    } else {
        std::println("[Audio] Unsupported file type: {}", filepath);
        return false;
    }
}

bool AudioPlayer::loadSamples(std::size_t sampleRate, std::size_t channels, std::span<const int16_t> samples) {
    if (samples.empty()) {
        std::println("[Audio] Invalid sample data.");
        return false;
    }

    _sampleRate = static_cast<ALsizei>(sampleRate);
    _channels   = static_cast<int>(channels);
    _format     = (_channels == 2) ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16;

    _audioData.clear();
    _audioData.insert(_audioData.end(), samples.begin(), samples.end());

    std::println("[Audio] Loaded samples: {} Hz, {} channels, {} frames.", _sampleRate, _channels, samples.size() / channels);
    return true;
}

bool AudioPlayer::loadWav(const std::string& filepath) {
    drwav wav;
    if (!drwav_init_file(&wav, filepath.c_str(), nullptr)) {
        std::println("[Audio] Failed to open WAV file: {}", filepath);
        return false;
    }

    drwav_uint64 totalFrames = wav.totalPCMFrameCount;
    std::vector<int16_t> tempData(static_cast<std::size_t>(totalFrames * wav.channels));
    drwav_read_pcm_frames_s16(&wav, totalFrames, tempData.data());

    drwav_uninit(&wav);

    return loadSamples(wav.sampleRate, wav.channels, tempData);
}

bool AudioPlayer::loadOgg(const std::string& filepath) {
    int error = 0;
    stb_vorbis* vorbis = stb_vorbis_open_filename(filepath.c_str(), &error, nullptr);
    if (!vorbis) {
        std::println("[Audio] Failed to open OGG file: {} (error {})", filepath, error);
        return false;
    }

    stb_vorbis_info info = stb_vorbis_get_info(vorbis);
    unsigned int frames  = stb_vorbis_stream_length_in_samples(vorbis);

    std::vector<int16_t> tempData(frames * info.channels);
    stb_vorbis_get_samples_short_interleaved(vorbis, info.channels, tempData.data(), frames * info.channels);

    stb_vorbis_close(vorbis);

    return loadSamples(info.sample_rate, info.channels, tempData);
}

void AudioPlayer::play(bool loop) {
    _loop         = loop;
    _playing      = true;
    _streamActive = true;

    alSourceStop(_source);
    ALint queued = 0;
    alGetSourcei(_source, AL_BUFFERS_QUEUED, &queued);
    while (queued-- > 0) {
        unsigned int buffer;
        alSourceUnqueueBuffers(_source, 1, &buffer);
    }

    for (int i = 0; i < 2; ++i) {
        if (!queueInitialChunk(_buffers[i])) break;
    }

    alSourcePlay(_source);
}

bool AudioPlayer::queueInitialChunk(unsigned int buffer) {
    std::size_t remaining = _audioData.size() - _cursor;
    if (remaining == 0) {
        if (_loop) {
            _cursor   = 0;
            remaining = _audioData.size();
        } else {
            return false;
        }
    }

    std::size_t sendSize = std::min(_chunkSize, remaining);
    alBufferData(buffer, _format, _audioData.data() + _cursor, static_cast<ALsizei>(sendSize * sizeof(int16_t)), _sampleRate);
    alSourceQueueBuffers(_source, 1, &buffer);

    _cursor += sendSize;
    return true;
}

void AudioPlayer::stop() {
    _streamActive = false;
    _playing      = false;
}

void AudioPlayer::streamLoop() {
    auto queueChunk = [&](unsigned int buffer) {
        std::size_t remaining = _audioData.size() - _cursor;
        if (remaining == 0) {
            if (_loop) {
                _cursor   = 0;
                remaining = _audioData.size();
            } else {
                return false;
            }
        }

        std::size_t sendSize = std::min(_chunkSize, remaining);
        alBufferData(buffer, _format, _audioData.data() + _cursor, static_cast<ALsizei>(sendSize * sizeof(int16_t)), _sampleRate);
        alSourceQueueBuffers(_source, 1, &buffer);

        _cursor += sendSize;
        return true;
    };

    alSourceStop(_source);
    alSourcei(_source, AL_BUFFER, 0);

    std::size_t counter = 0UZ;
    while (!_terminate) {
        if (alcGetCurrentContext() != _context) {
            std::println("[Audio] Context lost, re-binding OpenAL context.");
            alcMakeContextCurrent(_context);
        }

        if (_streamActive) {
            counter++;
            if (counter % 200UZ == 0UZ) {
                std::println("[Audio] streamLoop() running for {} - tabVisible: {}", counter, isTabVisible());
            }

            ALint processed = 0;
            alGetSourcei(_source, AL_BUFFERS_PROCESSED, &processed);

            while (processed-- > 0) {
                unsigned int buffer;
                alSourceUnqueueBuffers(_source, 1, &buffer);

                if (!queueChunk(buffer)) {
                    _streamActive = false;
                    _playing      = false;
                    break;
                }
            }

            ALint state;
            alGetSourcei(_source, AL_SOURCE_STATE, &state);
            if (state != AL_PLAYING && _playing) {
                alSourcePlay(_source);
            }

            ALint queuedBuffers = 0;
            alGetSourcei(_source, AL_BUFFERS_QUEUED, &queuedBuffers);
            if (queuedBuffers == 0 && _playing) {
                std::println("[Audio] Buffer underrun detected. Attempting recovery...");
                for (int i = 0; i < 2; ++i) {
                    if (!queueInitialChunk(_buffers[i])) break;
                }
                alSourcePlay(_source);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    alSourceStop(_source);
    ALint queued = 0;
    alGetSourcei(_source, AL_BUFFERS_QUEUED, &queued);
    while (queued-- > 0) {
        unsigned int buffer;
        alSourceUnqueueBuffers(_source, 1, &buffer);
    }
}
