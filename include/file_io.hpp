#ifndef FILE_IO_HPP
#define FILE_IO_HPP

#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <expected>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <shared_mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>
#include <fstream>

#include <EmscriptenHelper.hpp>
#include <LockFreeQueue.hpp>

namespace file {

template<typename T>
concept ChronoDuration = requires {
    typename T::rep;
    typename T::period;
    requires std::same_as<std::remove_cvref_t<decltype(T::zero())>, T>;
};

struct FileData {
    std::size_t          requestID;
    std::string          name;
    std::vector<uint8_t> data;
};

class Request {
    using DataStoreType = std::expected<std::vector<FileData>, std::string>;

    struct SharedState {
        std::size_t              requestID{0UZ};
        DataStoreType            result{std::unexpected("initialised")};
        std::atomic<std::size_t> pendingUsers{1UZ};
    };

    std::shared_ptr<SharedState> _state = std::make_shared<SharedState>();

    void complete(std::vector<FileData> files) { _state->result = std::move(files); }
    void completeWithError(std::string errorMsg) { _state->result = std::unexpected(std::move(errorMsg)); }

public:
    Request() = delete;

    explicit Request(std::size_t requestID) : _state(std::make_shared<SharedState>(requestID)) {}

    Request(const Request& other) noexcept : _state(other._state) { _state->pendingUsers.fetch_add(1UZ, std::memory_order_relaxed); }
    Request(Request&& other) = default;

    Request& operator=(const Request& other) noexcept {
        if (this != &other) {
            _state->pendingUsers.fetch_sub(1UZ, std::memory_order_relaxed);
            _state = other._state;
            _state->pendingUsers.fetch_add(1UZ, std::memory_order_relaxed);
        }
        return *this;
    }

    Request& operator=(Request&& other) noexcept {
        if (this != &other) {
            _state->pendingUsers.fetch_sub(1UZ, std::memory_order_relaxed);
            _state = std::move(other._state);
            // no increment needed
        }
        return *this;
    }

    ~Request() noexcept {
        if (_state && _state->pendingUsers.fetch_sub(1UZ, std::memory_order_acq_rel) == 2UZ) {
            _state->pendingUsers.notify_all();
        }
    }

    std::size_t requestID() const noexcept { return _state->requestID; }
    std::size_t refCount() const noexcept { return _state.use_count(); }
    bool        isOwner() const { return refCount() == 1UZ; }

    DataStoreType& get() {
        // if (!isOwner()) {
        //     throw std::logic_error("request is co-owned by another thread - check isOwner first");
        // }
        return _state->result;
    }

    template<ChronoDuration DurationType = std::chrono::milliseconds>
    bool wait(DurationType timeout = std::chrono::milliseconds(0), std::size_t divider = 10UZ) const {
        std::size_t initialRefCount = refCount();
        if (initialRefCount == 1UZ) {
            return true;
        }

        if (timeout.count() == 0 && !isMainThread()) {
            // wait indefinitely or until new data arrived
            std::size_t expected = _state->pendingUsers.load(std::memory_order_relaxed);
            while (expected == initialRefCount) {
                _state->pendingUsers.wait(expected, std::memory_order_relaxed);
                expected = _state->pendingUsers.load(std::memory_order_relaxed);
            }
            return true;
        }

        if (timeout.count() == 0 && isMainThread()) {
            std::println(stderr, "[WARNING] called wait() in main WASM thread -> returning false");
            return false;
        }

        auto start = std::chrono::steady_clock::now();
        auto slice = timeout / divider;

        while (true) {
            if (refCount() == 1UZ) {
                return true;
            }

            std::println("waiting for: {}", slice.count());
            std::this_thread::yield();
            std::this_thread::sleep_for(slice);

            if ((std::chrono::steady_clock::now() - start) >= timeout) {
                return false; // timeout
            }
        }
    }

    friend class FileIo;
};

using HttpLoadCallback   = std::function<std::vector<FileData>(std::size_t requestID, std::string_view path, std::string_view accept, bool multipleFiles)>;
using FileDialogCallback = std::function<std::vector<FileData>(std::size_t requestID, std::string_view path, std::string_view accept, bool multipleFiles)>;

class FileIo {
    std::atomic<std::size_t>    _requestID     = {0UZ};
    std::atomic<std::size_t>    _updateCounter = {0UZ};
    LockFreeQueue<FileData, 64> _uploadedFiles;
    LockFreeQueue<FileData, 64> _pendingWrites;
    HttpLoadCallback            _httpLoader = [this](std::size_t requestID, std::string_view url, std::string_view /*accept*/, bool /*multipleFiles*/) { return this->triggerHttpLoad(requestID, url); };
    FileDialogCallback          _fileDialog = [this](std::size_t requestID, std::string_view /*path*/, std::string_view accept, bool multipleFiles) { return this->triggerFileUpload(requestID, accept, multipleFiles); };

    std::vector<FileData> triggerHttpLoad(std::size_t requestID, std::string_view url);
    std::vector<FileData> triggerFileUpload(std::size_t requestID, std::string_view accept, bool multipleFiles);

    std::mutex                               _requestsMutex;
    std::unordered_map<std::size_t, Request> _pendingRequests;

    FileIo() = default; // use instance() singleton
public:
    [[maybe_unused]] Request loadFile(std::string_view source = {}, std::string_view acceptedFileExtensions = "", bool acceptMultipleFiles = true); // empty source launches browser picker
    void                     pushUploadedFiles(std::vector<FileData> files) noexcept {
        bool found = false;
        {
            std::scoped_lock lock(_requestsMutex);
            if (auto it = _pendingRequests.find(files[0UZ].requestID); it != _pendingRequests.end()) {
                std::println("pushUploadedFiles: Matching request for ID {}", files[0UZ].requestID);
                it->second.complete(files);
                // Request request = it->second;
                _pendingRequests.erase(it);
                found = true;
            } else {
                std::println("pushUploadedFiles: No matching request for ID {}", files[0UZ].requestID);
                return;
            }
        }

        for (FileData& file : files) {
            _uploadedFiles.push_back(std::move(file));
            _updateCounter.fetch_add(1UZ, std::memory_order_relaxed);
            _updateCounter.notify_all();
        }

        std::println("pushUploadedFiles: notify file upload: {} - counter: {} found: {}", files[0].name, _updateCounter.load(), found);
    }

    [[nodiscard]] std::vector<FileData> pollUploadedFile(std::optional<std::size_t> requestID = std::nullopt) noexcept {
        std::vector<FileData> matches;

        std::optional<FileData> file = _uploadedFiles.pop_front();
        if (requestID.has_value()) {
            std::deque<FileData> nonMatching;
            for (; file.has_value(); file = _uploadedFiles.pop_front()) {
                if (file->requestID == *requestID) {
                    matches.push_back(std::move(*file));
                } else {
                    // non-matching file -> back into the queue
                    nonMatching.push_back(std::move(*file));
                }
            }
            for (auto& nonMatchingFile : nonMatching) {
                _uploadedFiles.push_back(std::move(nonMatchingFile));
            }
        } else {
            // get all
            for (; file.has_value(); file = _uploadedFiles.pop_front()) {
                matches.push_back(std::move(*file));
            }
        }

        return matches;
    }

    template<ExecutionMode mode = ExecutionMode::Async, std::ranges::contiguous_range Data = std::vector<std::uint8_t>>
    void writeFile(std::string_view path, Data&& data);
    void processPendingWrites();

    void setHttpLoadCallback(HttpLoadCallback cb) { _httpLoader = std::move(cb); }
    void setFileDialogCallback(FileDialogCallback cb) { _fileDialog = std::move(cb); }

    static FileIo& instance() noexcept {
        static FileIo singleton;
        return singleton;
    }
};

template<ExecutionMode mode, std::ranges::contiguous_range Data>
void FileIo::writeFile(std::string_view path, Data&& data) {
    if (!isMainThread() && mode == ExecutionMode::Async) {
        _pendingWrites.push_back(FileData{.requestID = 0UZ, .name = std::string(path), .data = data});
        return;
    }
    if constexpr (mode == ExecutionMode::Sync) {
        // sync mode -> automatically flush previous files
        processPendingWrites();
    }

#ifdef __EMSCRIPTEN__
    EM_ASM(
        {
            const filename = UTF8ToString($0);
            const len      = $2;
            const array    = new Uint8Array(len);
            array.set(HEAPU8.subarray($1, $1 + len));

            const blob = new Blob([array], {
                type:
                    'application/octet-stream'
            });
            const link    = document.createElement('a');
            link.href     = URL.createObjectURL(blob);
            link.download = filename;
            document.body.appendChild(link);

            // N.B. try-catch needed to simulate click to trigger download
            // may fail based on browser security settings (i.e. auto-accept downloads)
            try {
                link.click();
            } catch (e) {
                console.error("[EM_ASM] FileIo::writeFile(..) - link.click() failed: ", e);
                return;
            }

            try {
                document.body.removeChild(link);
            } catch (e) {
                console.error("[EM_ASM] FileIo::writeFile(..) - removeChild failed: ", e);
                return;
            }

            try {
                URL.revokeObjectURL(link.href);
            } catch (e) {
                console.error("[EM_ASM] FileIo::writeFile(..) - URL.revokeObjectURL failed: ", e);
                return;
            }
        },
        path.data(), data.data(), static_cast<int>(data.size()));
#else
    try {
        std::ofstream out(std::string(path), std::ios::binary);
        if (!out) {
            throw std::runtime_error(std::format("Failed to open file for writing: '{}'", path));
        }
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        std::println("[FileIo] File written: {} ({} bytes)", path, data.size());
    } catch (const std::exception& e) {
        std::println("[FileIo] Error writing file: {}", e.what());
    }
#endif
}

} // namespace file

#ifdef __EMSCRIPTEN__
extern "C" void handle_uploaded_files(std::size_t requestID, const uint8_t* buffer, int length);
#endif

#endif // FILE_IO_HPP