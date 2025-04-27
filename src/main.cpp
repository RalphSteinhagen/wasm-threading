#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <atomic>
#include <chrono>
#include <format>
#include <optional>
#include <print>
#include <string>
#include <thread>
#include <vector>

#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_sdl3.h>
#include <imgui.h>

#include <Clipboard.hpp>
#include <EmscriptenHelper.hpp>
#include <audio.hpp>
#include <audio_sdl.hpp>
#include <file_io.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
extern "C" void initPassiveTouchListeners();
#endif

SDL_Window*       g_Window    = nullptr;
SDL_GLContext     g_GLContext = nullptr;
std::atomic<bool> g_Running{true};

std::atomic<bool> g_BackgroundTaskRunning{false};
std::atomic<bool> g_TriggerTask{false};
std::thread       g_BackgroundThread;

AudioPlayer    g_Audio;
static bool    g_AudioStarted = false;
SdlAudioPlayer g_Audio_sdl;
static bool    g_AudioStarted_sdl = false;

static std::optional<file::FileData> g_Uploaded;

bool duplicateUploadedFile(const std::string& baseName, const std::vector<uint8_t>& data, int count = 5) {
    if (data.empty()) {
        std::println("[FileIO] No data to duplicate.");
        return false;
    }
    bool ok = true;
    for (int i = 0; i < count; ++i) {
        std::string dupName = std::format("{}_{}", baseName, i);
        try {
            file::FileIo::instance().writeFile(dupName, data);
            std::println("[FileIO] Duplicate created: {}", dupName);
        } catch (const std::exception& e) {
            std::println("[FileIO] Write failed: {}", e.what());
            ok = false;
        }
    }
    return ok;
}

void backgroundProcessingLoop() {
    std::println("[Background] Started backgroundProcessingLoop() - WASM main thread: {}", isMainThread());
    while (g_Running.load()) {
        if (g_TriggerTask.exchange(false)) {
            g_BackgroundTaskRunning.store(true);
            std::println("[Background] Started long task");
            for (int i = 0; i < 5; ++i) {
                std::string fileName = std::format("test_file_{}.txt", i);
                file::FileIo::instance().writeFile(fileName, {'H', 'e', 'l', 'l', 'o'});
            }
            std::this_thread::sleep_for(std::chrono::seconds(3));
            std::println("[Background] Finished long task");
            g_BackgroundTaskRunning.store(false);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}

void renderFrame() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_QUIT) {
            g_Running = false;
        }
    }

    file::FileIo::instance().processPendingWrites();
    if (auto newUploads = file::FileIo::instance().pollUploadedFile(); !newUploads.empty()) {
        for (auto& newFile : newUploads) {
            g_Uploaded = std::move(newFile);
            std::println("[Main] Got upload: ID={} Name={} ({} bytes)", g_Uploaded->requestID, g_Uploaded->name, g_Uploaded->data.size());
        }
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Tasks & FileIO", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    if (ImGui::Button("Run Long Task") && !g_BackgroundTaskRunning.load()) {
        g_TriggerTask = true;
    }
    ImGui::SameLine();
    ImGui::Text(g_BackgroundTaskRunning ? "Background task is running..." : "Idle.");

    if (!g_AudioStarted && ImGui::Button("Start OpenAL Audio")) {
        g_AudioStarted = true;
        if (!g_Audio.load("assets/audio/sample2.ogg")) {
            std::println("[Audio] Load failed");
        } else {
            g_Audio.play();
        }
    } else if (g_AudioStarted && ImGui::Button("Stop OpenAL Audio")) {
        g_AudioStarted = false;
        g_Audio.stop();
    }

    if (!g_AudioStarted_sdl && ImGui::Button("Start SDL Audio")) {
        g_AudioStarted_sdl = true;
        if (!g_Audio_sdl.load("assets/audio/sample2.ogg")) {
            std::println("[Audio] Load failed");
        } else {
            g_Audio_sdl.play();
        }
    } else if (g_AudioStarted_sdl && ImGui::Button("Stop SDL Audio")) {
        g_AudioStarted_sdl = false;
        g_Audio_sdl.stop();
    }

    static file::Request uploadRequest(0);
    if (ImGui::Button("Upload File")) {
        uploadRequest = file::FileIo::instance().loadFile();
    }
    ImGui::SameLine();
    if (uploadRequest.isOwner() && uploadRequest.get().has_value()) { // demo for async behaviour
        ImGui::Text("Uploaded %zu files - first: %s (%zu bytes)", uploadRequest.get().value().size(), uploadRequest.get().value()[0].name.c_str(), uploadRequest.get().value()[0].data.size());
    } else {
        ImGui::Text("No uploaded file yet.");
    }

    static file::Request urlRequest1(0);
    if (ImGui::Button("Load from URL")) {
        std::thread run([] {
            urlRequest1 = file::FileIo::instance().loadFile("https://upload.wikimedia.org/wikipedia/commons/thumb/5/54/FAIR_Logo_rgb.png/330px-FAIR_Logo_rgb.png");
        });
        run.detach();

        // normally only used in non-WASM apps:
        // urlRequest1.wait(); // wait indefinitely
        // urlRequest1.wait(std::chrono::milliseconds(100)); // wait with time-out
    }
    ImGui::SameLine();
    if (urlRequest1.isOwner() && urlRequest1.get().has_value()) { // demo for async behaviour
        ImGui::Text("Uploaded URL: %s (%zu bytes)", urlRequest1.get().value()[0].name.c_str(), urlRequest1.get().value()[0].data.size());
    } else {
        ImGui::Text("No URL file uploaded.");
    }

    static file::Request urlRequest2(0);
    if (ImGui::Button("Load from URL (worker thread)")) {
        std::thread run([] {
            urlRequest2 = file::FileIo::instance().loadFile("https://upload.wikimedia.org/wikipedia/commons/thumb/5/54/FAIR_Logo_rgb.png/330px-FAIR_Logo_rgb.png");
        });
        run.detach();
        if (urlRequest2.wait()) {
            std::println("[Main] Waiting for request received:\n{}",
                (urlRequest2.isOwner() && urlRequest2.get().has_value())? urlRequest2.get().value()[0].name.c_str() : " nothing");
        }
    }
    ImGui::SameLine();
    if (urlRequest2.isOwner() && urlRequest2.get().has_value()) { // demo for async behaviour
        ImGui::Text("Uploaded URL: %s (%zu bytes)", urlRequest2.get().value()[0].name.c_str(), urlRequest2.get().value()[0].data.size());
    } else {
        ImGui::Text("No URL file uploaded.");
    }

    static file::Request uploadPath(0);
    if (ImGui::Button("Load from Path")) {
        uploadPath = file::FileIo::instance().loadFile("assets/audio/sample2.ogg");
    }
    ImGui::SameLine();
    if (uploadPath.isOwner() && uploadPath.get().has_value()) { // demo for async behaviour
        ImGui::Text("Uploaded path: %s (%zu bytes)", uploadPath.get().value()[0].name.c_str(), uploadPath.get().value()[0].data.size());
    } else {
        ImGui::Text("No file from path (yet).");
    }

    if (g_Uploaded) {
        ImGui::Text("Uploaded: %s (%zu bytes)", g_Uploaded->name.c_str(), g_Uploaded->data.size());
    } else {
        ImGui::Text("No file uploaded.");
    }

    if (ImGui::Button("Download Duplicates")) {
        if (g_Uploaded && !g_Uploaded->data.empty()) {
            duplicateUploadedFile(g_Uploaded->name, g_Uploaded->data);
        } else {
            std::println("[FileIO] No previously uploaded file available.");
        }
    }

    if (ImGui::Button("List Persistent Files")) {
        listPersistentFiles();
    }

    if (ImGui::Button("Clipboard Test")) {
        clipboard::writeText("Hello from C++!");
        std::string text = clipboard::readText();
        if (!text.empty()) {
            std::println("[SYNC] Clipboard contains: {}", text);
        }
        clipboard::readTextAsync([](const std::string& asyncText) { std::println("[ASYNC] Clipboard contains: {}", asyncText); }, [](const std::string& error) { std::println("[ASYNC] Clipboard read error: {}", error); });
        clipboard::queryClipboardTypes(
            [](const std::vector<std::string>& types) {
                for (const auto& type : types) {
                    std::println(" - {}", type);
                }
            },
            [](const std::string& error) { std::println("[TYPES] Clipboard type query error: {}", error); });
    }
    ImGui::End();

    ImGui::Render();
    int fbWidth = 0, fbHeight = 0;
    SDL_GetWindowSizeInPixels(g_Window, &fbWidth, &fbHeight);
    glViewport(0, 0, fbWidth, fbHeight);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(g_Window);
}

#ifdef __EMSCRIPTEN__
void emscriptenMainLoop() {
    if (!g_Running) {
        emscripten_cancel_main_loop();
        return;
    }
    try {
        renderFrame();
    } catch (...) {
        std::println("[emscriptenMainLoop] Caught unknown exception");
    }
}
#endif


bool requestGLContext(int major, int minor) {
    std::println("[Main] Requesting OpenGL context {}.{}", major, minor);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, major);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, minor);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

#ifndef __EMSCRIPTEN__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES); // emulate GLES (WebGL)
#endif

    g_Window = SDL_CreateWindow("ImGui + SDL3 + FileIO", 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!g_Window) {
        std::println("[Main] SDL_CreateWindow failed: '{}'", SDL_GetError());
        return false;
    }

    g_GLContext = SDL_GL_CreateContext(g_Window);
    if (!g_GLContext) {
        std::println("[Main] SDL_GL_CreateContext failed ({}.{})", major, minor);
        SDL_DestroyWindow(g_Window);
        g_Window = nullptr;
        return false;
    }

    return true;
}


bool initSDL() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        std::println("[Main] SDL_Init failed: '{}'", SDL_GetError());
        return false;
    }

    if (!requestGLContext(3, 3) && !requestGLContext(2, 0)) {
        std::println("[Main] Could not create any GL context!");
        SDL_Quit();
        return false;
    }

    SDL_GL_SetSwapInterval(1);
    return true;
}

bool initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplSDL3_InitForOpenGL(g_Window, g_GLContext);

#ifdef __EMSCRIPTEN__
    initPassiveTouchListeners();
    // WebGL1 = #version 100, WebGL2 = #version 300 es
    return ImGui_ImplOpenGL3_Init("#version 100");
#else
    return ImGui_ImplOpenGL3_Init("#version 330 core");
#endif
}

int main() {
    std::println("[Main] Starting main loop...");

    if (!initSDL()) {
        return 1;
    }

    if (!initImGui()) {
        std::println("[Main] ImGui initialisation failed.");
        SDL_GL_DestroyContext(g_GLContext);
        SDL_DestroyWindow(g_Window);
        SDL_Quit();
        return 1;
    }

    g_BackgroundThread = std::thread(backgroundProcessingLoop);

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(emscriptenMainLoop, 0, true);
#else
    while (g_Running) {
        renderFrame();
    }
#endif

    g_Running = false;
    if (g_BackgroundThread.joinable()) {
        g_BackgroundThread.join();
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(g_GLContext);
    SDL_DestroyWindow(g_Window);
    SDL_Quit();

    return 0;
}
