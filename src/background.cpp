#include "background.hpp"
#include <chrono>
#include <format>
#include <print>

#include <file_io.hpp>

BackgroundProcessor::BackgroundProcessor() : _running(false) {}

BackgroundProcessor::~BackgroundProcessor() {
    stop();
}

void BackgroundProcessor::start() {
    std::println("started start()");
    _running = true;
    _thread = std::thread(&BackgroundProcessor::process, this);
}

void BackgroundProcessor::stop() {
    _running = false;
    if (_thread.joinable()) {
        _thread.join();
    }
}

void BackgroundProcessor::process() {
    std::println("started process()");
    file::FileIo::instance().writeFile("initial_file.txt", {'H', 'e', 'l', 'l', 'o'});
    int i = 0;
    while (_running) {
        // Perform background tasks here
        std::string fileName = std::format("test_file_{}.txt", i);
        std::println("writing to file: {}", fileName);
        file::FileIo::instance().writeFile(fileName, {'H', 'e', 'l', 'l', 'o'});
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    }
}
