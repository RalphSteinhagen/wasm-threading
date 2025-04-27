#ifndef EMSCRIPTENHELPER_HPP
#define EMSCRIPTENHELPER_HPP

#ifdef __EMSCRIPTEN__
#include <emscripten/threading.h>
#include <emscripten/fetch.h>
#include <emscripten/html5.h>
#endif

enum class ExecutionMode : std::uint8_t { Async = 0, Sync };

constexpr bool isWebAssembly() noexcept {
#ifdef __EMSCRIPTEN__
    return true;
#else
    return false;
#endif
}

inline bool isMainThread() {
#ifdef __EMSCRIPTEN__
    return emscripten_is_main_runtime_thread();
#else
    return true; // Native: assume single-threaded or main thread
#endif
}

inline bool isTabVisible() {
#ifdef __EMSCRIPTEN__
    EmscriptenVisibilityChangeEvent status;
    if (emscripten_get_visibility_status(&status) == EMSCRIPTEN_RESULT_SUCCESS) {
        return !status.hidden;
    }
#endif
    return true;
}

inline void listPersistentFiles(bool recursive = true) {
#ifdef __EMSCRIPTEN__
    EM_ASM_({
        function listDir(path, recursive, indent = "") {
            try {
                const entries = FS.readdir(path);
                for (let entry of entries) {
                    if (entry === '.' || entry === '..') continue;
                    const fullPath = path + (path.endsWith('/') ? "" : "/") + entry;
                    const stat = FS.stat(fullPath);
                    if (FS.isDir(stat.mode)) {
                        console.log(indent + '[Dir] ' + fullPath);
                        if (recursive) {
                            listDir(fullPath, recursive, indent + '  ');
                        }
                    } else {
                        console.log(indent + '[File] ' + fullPath);
                    }
                }
            } catch (e) {
                console.error('Error listing directory:', path, e);
            }
        }
        listDir('/', $0 !== 0);
    }, recursive ? 1 : 0);
#else

#endif
}


#endif //EMSCRIPTENHELPER_HPP