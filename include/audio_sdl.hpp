// audio_sdl.hpp
#ifndef AUDIO_SDL_HPP
#define AUDIO_SDL_HPP

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>

class SdlAudioPlayer {
public:
    SdlAudioPlayer();
    ~SdlAudioPlayer();

    bool load(const std::string& filepath);
    void play(bool loop = true);
    void stop();

private:
    void streamLoop();
    bool loadWav(const std::string& filepath);
    bool loadOgg(const std::string& filepath);

    SDL_AudioDeviceID    _device = 0;
    SDL_AudioSpec        _spec{};
    SDL_AudioStream*     _stream = nullptr;
    std::vector<uint8_t> _audioData;

    std::thread       _streamThread;
    std::atomic<bool> _streamActive{false};
    std::atomic<bool> _playing{false};
    std::atomic<bool> _loop{false};
    std::atomic<bool> _terminate{false};

    std::size_t _cursor    = 0;
    std::size_t _chunkSize = 4096;
};

#endif // AUDIO_SDL_HPP