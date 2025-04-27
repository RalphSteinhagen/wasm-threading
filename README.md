# WASM/Native Threading Abstraction

This toy project provides a modern C++23 proof-of-concept for building portable applications targeting both **native** and **WebAssembly** (Emscripten) environments.  
It aims at testing various strategies for lightweight abstractions for:

- Async File I/O (upload, download, in-memory buffers)
- Async Audio playback (SDL/OpenAL)
- Clipboard access
- Background task handling (lock-free)
- ImGui-based user interfaces (SDL3 + OpenGL ES 2.0/WebGL2 backend)

Please use as you see fit.

## Building

### Native (Linux/macOS/Windows)

```bash
mkdir build-native
cd build-native
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j
./ImGuiEmscriptenApp
```

### WebAssembly (WASM via Emscripten >=4.x.y)

```bash
source /path/to/emsdk/emsdk_env.sh
mkdir build-wasm
cd build-wasm
emcmake cmake .. -DCMAKE_BUILD_TYPE=Release
make -j
python3 ../serve.py
# Access http://localhost:8000 in your browser
```

## License

See [LICENSE](LICENSE) for detail. Contact me if you have further questions or suggestions.