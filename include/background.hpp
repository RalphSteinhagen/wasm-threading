#ifndef BACKGROUND_HPP
#define BACKGROUND_HPP

#include <thread>
#include <atomic>

class BackgroundProcessor {
    void process();
    std::thread _thread;
    std::atomic<bool> _running;

public:
    BackgroundProcessor();
    ~BackgroundProcessor();
    void start();
    void stop();
};

#endif //BACKGROUND_HPP
