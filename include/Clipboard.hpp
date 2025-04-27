#ifndef CLIPBOARD_HPP
#define CLIPBOARD_HPP

#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <iostream>

#include <imgui.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/val.h>

extern "C" {

    // C-linkage for JS to call
    EMSCRIPTEN_KEEPALIVE void clipboard_on_success(const char* text);
    EMSCRIPTEN_KEEPALIVE void clipboard_on_error(const char* error);

} // extern "C"
#endif

namespace clipboard {

// --- Write Text ---
inline void writeText(const std::string& text) {
#ifdef __EMSCRIPTEN__
    emscripten::val::global("navigator")
        ["clipboard"].call<void>("writeText", emscripten::val(text));
#else
    ImGui::SetClipboardText(text.c_str());
#endif
}


// --- Read Text (sync, only native) ---
inline std::string readText() {
#ifndef __EMSCRIPTEN__
    const char* text = ImGui::GetClipboardText();
    return text ? std::string(text) : std::string{};
#else
    return {};
#endif
}


// --- Read Text (async, always safe) ---
inline void readTextAsync(std::function<void(std::string)> onSuccess, std::function<void(std::string)> onError = {}) {
#ifdef __EMSCRIPTEN__
    static std::function<void(std::string)>* successHandler = nullptr;
    static std::function<void(std::string)>* errorHandler   = nullptr;

    successHandler = new std::function<void(std::string)>(std::move(onSuccess));
    errorHandler   = new std::function<void(std::string)>(std::move(onError));

    EM_ASM({
        navigator.clipboard.readText().then(function(text) {
            var lengthBytes = lengthBytesUTF8(text) + 1;
            var stringOnWasmHeap = _malloc(lengthBytes);
            stringToUTF8(text, stringOnWasmHeap, lengthBytes);
            _clipboard_on_success(stringOnWasmHeap);
            _free(stringOnWasmHeap);
            }).catch(function(err) {
            var lengthBytes = lengthBytesUTF8(err.message) + 1;
            var stringOnWasmHeap = _malloc(lengthBytes);
            stringToUTF8(err.message, stringOnWasmHeap, lengthBytes);
            _clipboard_on_error(stringOnWasmHeap);
            _free(stringOnWasmHeap);
            });
        });
#else
    if (onSuccess) {
        const char* text = ImGui::GetClipboardText();
        onSuccess(text ? std::string(text) : std::string{});
    }
#endif
}

// --- Query available types ---
inline void queryClipboardTypes(std::function<void(std::vector<std::string>)> onSuccess, std::function<void(std::string)> onError = {}) {
#ifdef __EMSCRIPTEN__
    if (onSuccess) {
        onSuccess({"text/plain"});
    }
#else
    if (onSuccess) {
        onSuccess({"text/plain"});
    }
#endif
}

} // namespace clipboard

// --- Callback implementations ---

#ifdef __EMSCRIPTEN__
extern "C" {

    void clipboard_on_success(const char* text) {
        std::cout << "[Clipboard] Read Success: " << (text ? text : "(null)") << std::endl;
    }

    void clipboard_on_error(const char* error) {
        std::cout << "[Clipboard] Read Error: " << (error ? error : "(null)") << std::endl;
    }

} // extern "C"
#endif

#endif // CLIPBOARD_HPP