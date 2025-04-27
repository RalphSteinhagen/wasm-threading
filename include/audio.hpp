#ifndef AUDIO_HPP
#define AUDIO_HPP

#include <atomic>
#include <string>
#include <thread>
#include <span>
#include <vector>

#include <AL/al.h>
#include <AL/alc.h>

class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();
    bool load(const std::string& filepath);
    void play(bool loop = true);
    void stop();
    bool loadSamples(std::size_t sampleRate, std::size_t channels, std::span<const int16_t> samples);

private:
    bool queueInitialChunk(unsigned int buffer);
    void streamLoop();
    bool loadWav(const std::string& filepath);
    bool loadOgg(const std::string& filepath);

    ALCdevice*                _device     = nullptr;
    ALCcontext*               _context    = nullptr;
    ALenum                    _format     = AL_FORMAT_MONO16;
    ALsizei                   _sampleRate = 44100;
    unsigned int              _buffers[2]{}; // double-buffering
    unsigned int              _source = 0;
    std::vector<std::int16_t> _audioData;
    std::size_t                       _channels  = 1;
    std::size_t               _chunkSize = 8192;
    std::size_t               _cursor    = 0;

    std::thread       _streamThread;
    std::atomic<bool> _streamActive{false}; // Feeding active
    std::atomic<bool> _playing{false}; // Audio source playing
    std::atomic<bool> _loop{false}; // Should loop?
    std::atomic<bool> _terminate{false}; // Shut down thread
};

#endif // AUDIO_HPP