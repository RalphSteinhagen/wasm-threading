#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <print>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include <file_io.hpp>

namespace file {

std::vector<FileData> FileIo::triggerHttpLoad(std::size_t requestID, std::string_view url) {
#ifdef __EMSCRIPTEN__
    if (emscripten_is_main_runtime_thread()) { // call from within the main thread
        emscripten_fetch_attr_t attr;
        emscripten_fetch_attr_init(&attr);
        std::strcpy(attr.requestMethod, "GET");
        attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
        attr.userData   = new std::size_t(requestID);

        std::println("triggerHttpLoad - main thread ID: {}", std::this_thread::get_id());

        attr.onsuccess = [](emscripten_fetch_t* fetch) {
            auto ID = *static_cast<std::size_t*>(fetch->userData);
            delete static_cast<std::size_t*>(fetch->userData);

            std::println("triggerHttpLoad - onsuccess thread ID: {}", std::this_thread::get_id());

            try {
                std::string filename      = fetch->url;
                auto        filenameBytes = std::vector<uint8_t>(filename.begin(), filename.end());

                const uint32_t numFiles = 1;
                const uint32_t nameLen  = static_cast<uint32_t>(filenameBytes.size());
                const uint32_t dataLen  = static_cast<uint32_t>(fetch->numBytes);
                const size_t   totalSize = 4 + (4 + 4 + nameLen + dataLen);

                std::vector<uint8_t> buffer(totalSize);
                uint8_t* ptr = buffer.data();

                auto write_uint32 = [&ptr](uint32_t v) {
                    std::memcpy(ptr, &v, sizeof(v));
                    ptr += sizeof(v);
                };
                auto write_bytes = [&ptr](const uint8_t* data, size_t size) {
                    std::memcpy(ptr, data, size);
                    ptr += size;
                };

                write_uint32(numFiles);
                write_uint32(nameLen);
                write_uint32(dataLen);
                write_bytes(filenameBytes.data(), nameLen);
                write_bytes(reinterpret_cast<const uint8_t*>(fetch->data), dataLen);

                handle_uploaded_files(ID, buffer.data(), static_cast<int>(buffer.size()));
            } catch (const std::exception& e) {
                std::println("[FileIO] HttpLoad error: {}", e.what());
            }

            emscripten_fetch_close(fetch);
            std::println("triggerHttpLoad after - main thread ID: {}", std::this_thread::get_id());
        };

        emscripten_fetch(&attr, url.data());
    } else { // call from outside the main thread
        std::println("triggerHttpLoad outside - main thread ID: {}", std::this_thread::get_id());
        struct Args {
            std::size_t requestID;
            std::string url;
        };

        auto* args = new Args{requestID, std::string(url)};
        emscripten_async_run_in_main_runtime_thread(
            EM_FUNC_SIG_VI,
            +[](void* voidPtr) {
                std::unique_ptr<Args> a(static_cast<Args*>(voidPtr));
                FileIo::instance().triggerHttpLoad(a->requestID, a->url);
            },
            args
        );
    }
    return {};
#else
    throw std::runtime_error("No HttpLoadCallback registered"); // TODO: use HttpLib for this?
#endif
}

std::vector<FileData> FileIo::triggerFileUpload(std::size_t requestID, std::string_view accept, bool multipleFiles) {
#ifdef __EMSCRIPTEN__
    if (emscripten_is_main_runtime_thread()) {
        std::println("triggerFileUpload - main thread ID: {}", std::this_thread::get_id());

        // clang-format off
        EM_ASM(
            {
                const requestId     = $0;
                const acceptFilter  = UTF8ToString($1);
                const allowMultiple = $2;

                const input    = document.createElement('input');
                input.type     = 'file';
                input.multiple = allowMultiple;
                if (acceptFilter.length > 0) {
                    input.accept = acceptFilter;
                }

                input.onchange = (e) => {
                    const files = e.target.files;
                    if (!files || (files.length === 0)) {
                        console.warn("[FileIO] No files selected.");
                        return;
                    }

                    let totalSize = 4;
                    for (let i = 0; i < files.length; ++i) {
                        totalSize += 4 + 4; // nameLen + dataLen
                        totalSize += (new TextEncoder()).encode(files[i].name).length;
                        totalSize += files[i].size;
                    }

                    // Buffer Layout (binary format):
                    //   [uint32_t numFiles]
                    //   For each file:
                    //     [uint32_t nameLength]
                    //     [uint32_t dataLength]
                    //     [uint8_t nameBytes[nameLength]]   (UTF-8 encoded filename)
                    //     [uint8_t fileData[dataLength]]    (raw file content)

                    const buffer = new Uint8Array(totalSize);
                    const view   = new DataView(buffer.buffer);

                    let offset = 0;
                    view.setUint32(offset, files.length, true);
                    offset += 4;

                    const encoder = new TextEncoder();

                    let readersRemaining = files.length;
                    for (let i = 0; i < files.length; ++i) {
                        const file   = files[i];
                        const reader = new FileReader();

                        reader.onload = ((file) => (e) => {
                            const nameBytes = encoder.encode(file.name);
                            const dataBytes = new Uint8Array(e.target.result);

                            view.setUint32(offset, nameBytes.length, true);
                            offset += 4;
                            view.setUint32(offset, dataBytes.length, true);
                            offset += 4;
                            buffer.set(nameBytes, offset);
                            offset += nameBytes.length;
                            buffer.set(dataBytes, offset);
                            offset += dataBytes.length;

                            readersRemaining--;
                            if (readersRemaining === 0) {
                                const ptr = Module._malloc(buffer.length);
                                Module.HEAPU8.set(buffer, ptr);
                                Module.ccall('handle_uploaded_files', null, [ 'number', 'number', 'number' ], [ requestId, ptr, buffer.length ]);
                                Module._free(ptr);
                            }
                        })(file);

                        reader.readAsArrayBuffer(file);
                    }
                };

                input.click();
            },
            requestID, accept.data(), multipleFiles
        );
        // clang-format on
    } else {
        std::println("triggerFileUpload - outside main thread ID: {}", std::this_thread::get_id());

        struct Args {
            std::size_t requestID;
            std::string accept;
            bool multiple;
        };

        auto* args = new Args{requestID, std::string(accept), multipleFiles};

        emscripten_async_run_in_main_runtime_thread(
            EM_FUNC_SIG_VI,
            +[](void* voidPtr) {
                std::unique_ptr<Args> a(static_cast<Args*>(voidPtr));
                FileIo::instance().triggerFileUpload(a->requestID, a->accept, a->multiple);
            },
            args
        );
    }

    return {}; // Async
#else
    throw std::runtime_error("No FileDialogCallback registered");
#endif
}

[[maybe_unused]] Request FileIo::loadFile(std::string_view source, std::string_view acceptedFileExtensions, bool acceptMultipleFiles) {
    Request request(_requestID.fetch_add(1UZ, std::memory_order_relaxed));
    {
        std::scoped_lock lock(_requestsMutex);
        _pendingRequests.emplace(request.requestID(), request);
    }

    constexpr auto startsWith = [](std::string_view source, std::string_view prefix) -> bool { return source.size() >= prefix.size() && std::ranges::equal(prefix, source.substr(0, prefix.size()), [](char a, char b) { return std::tolower(a) == std::tolower(b); }); };

    if (source.empty()) {
        if (_fileDialog) {
            FileIo::instance().pushUploadedFiles(_fileDialog(request.requestID(), source, acceptedFileExtensions, acceptMultipleFiles));
        } else {
            std::println("[FileIO] No file dialog callback configured.");
        }
    } else if (startsWith(source, "http://") || startsWith(source, "https://")) {
        if (_httpLoader) {
            FileIo::instance().pushUploadedFiles(_httpLoader(request.requestID(), source, acceptedFileExtensions, acceptMultipleFiles));
        } else {
            std::println("[FileIO] No HTTP loader callback configured.");
        }
    } else {
        try {
            std::ifstream in(std::string(source), std::ios::binary);
            if (!in) {
                throw std::runtime_error("File open failed");
            }
            std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), {});
            FileIo::instance().pushUploadedFiles({FileData{.requestID = request.requestID(), .name = std::string(source), .data = std::move(data)}});
        } catch (const std::exception& e) {
            std::println("[FileIO] Error loading file: {}", e.what());
        }
    }
    return request;
}

void FileIo::processPendingWrites() {
    if (!isMainThread()) {
        return;
    }
    while (!_pendingWrites.empty()) {
        if (auto task = _pendingWrites.pop_front(); task) {
            writeFile(task->name, task->data);
        }
    }
}

} // namespace file

#ifdef __EMSCRIPTEN__
extern "C" void handle_uploaded_files(std::size_t requestID, const uint8_t* buffer, int length) {
    if (!buffer || length <= 0) {
        std::println("[FileIO] Invalid uploaded files buffer.");
        return;
    }

    // Buffer Layout (binary format):
    //   [uint32_t numFiles]
    //   For each file:
    //     [uint32_t nameLength]
    //     [uint32_t dataLength]
    //     [uint8_t nameBytes[nameLength]]   (UTF-8 encoded filename)
    //     [uint8_t fileData[dataLength]]    (raw file content)
    const uint8_t* ptr = buffer;
    const uint8_t* end = buffer + length;

    auto read_uint32 = [&ptr, end]() -> uint32_t {
        if (ptr + sizeof(uint32_t) > end) {
            throw std::runtime_error("Unexpected end of buffer while reading uint32_t");
        }
        uint32_t value;
        std::memcpy(&value, ptr, sizeof(uint32_t));
        ptr += sizeof(uint32_t);
        return value;
    };

    auto read_bytes = [&ptr, end](std::size_t count) -> std::vector<uint8_t> {
        if (ptr + count > end) {
            throw std::runtime_error("Unexpected end of buffer while reading bytes");
        }
        std::vector<uint8_t> result(ptr, ptr + count);
        ptr += count;
        return result;
    };

    try {
        uint32_t                    numFiles = read_uint32();
        std::vector<file::FileData> files;
        files.reserve(numFiles);

        for (uint32_t i = 0; i < numFiles; ++i) {
            uint32_t nameLen = read_uint32();
            uint32_t dataLen = read_uint32();

            auto name = read_bytes(nameLen);
            auto data = read_bytes(dataLen);

            files.push_back(file::FileData{
                .requestID = requestID,
                .name      = std::string(reinterpret_cast<const char*>(name.data()), name.size()),
                .data      = std::move(data),
            });
        }

        file::FileIo::instance().pushUploadedFiles(std::move(files));

    } catch (const std::exception& e) {
        std::println("[FileIO] Error parsing uploaded files: {}", e.what());
    }
}
#endif