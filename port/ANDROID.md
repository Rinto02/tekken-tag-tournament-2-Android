# Android port

Status: **milestone 1 in progress.** There is no APK yet and the game does not run on Android.

The PC port is a static recompilation of the Xbox 360 game, built for Windows on Direct3D 11.
Getting it onto Android means replacing every Windows-specific layer. The build and tests for
the pieces done so far run in GitHub Actions (`.github/workflows/android.yml`).

## What the Windows runtime depends on

Measured over `port/src/runtime` (about 64k lines):

| Area | Files | Android replacement |
| --- | --- | --- |
| `windows.h` | 48 | per-API replacements below |
| Direct3D 11 (renderer, movie backend) | 16 | Vulkan |
| XAudio2 | 10 | AAudio |
| Threads, events, SRW locks | 10 | `std::thread`, `platform::Event`, pthreads |
| `VirtualAlloc` / `VirtualProtect` / write watch | 6 | `platform` guest memory + write watch |
| SEH / vectored exception handlers | 10 | `platform` fault hooks (signals) |
| Winsock | 5 | POSIX sockets |
| SChannel TLS, CNG | 2 + 4 | a TLS library (BoringSSL / mbedTLS) |
| XInput | 3 | Android gamepad + touch input |
| Window creation | 1 | `ANativeWindow` / `GameActivity` |

The recompiled PPC code (`re/ppc`) is generated on your PC from your own game files and is not
in this repository. It uses SIMDe, so it can be compiled for ARM64.

## Milestones

1. **Platform layer** (`port/src/platform`): guest memory, write watch, fault hooks and crash
   reporting, clock, events. Done and tested on Linux host and built with the NDK in CI.
2. Move `src/runtime` onto the platform layer: threads, locks, file I/O, timers, logging, the
   guest-memory setup in `main.cpp`.
3. Audio: XAudio2 to AAudio.
4. Renderer: Direct3D 11 to Vulkan (`gpu_hw_d3d11.h` and the movie backend), including the
   shader translation output.
5. APK: Gradle project, `GameActivity`, touch overlay and gamepad input, game files loaded
   from device storage, CI that builds and uploads the APK.

## Building what exists

```
cmake -S port/android -B build-host && cmake --build build-host && ctest --test-dir build-host
```

For the NDK, see the comment at the top of `port/android/CMakeLists.txt`.
