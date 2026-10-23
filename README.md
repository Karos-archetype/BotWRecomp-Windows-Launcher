# BotWRecomp Windows Launcher

Experimental Windows x64 **launcher-only** pre-release, `v0.1.0-preview.1`.

This repository and its releases contain launcher code and the launcher executable only. They contain no ROM, RPX, recompiled guest/game executable, Nintendo game assets, game shaders, saves, keys, local configuration, or game downloads. The launcher cannot run the game on its own.

## Credits and contributors

Original project: [IshaanKalra2103/BotWRecomp](https://github.com/IshaanKalra2103/BotWRecomp), by Ishaan Kalra and the original BotWRecomp contributors. This Windows launcher work builds on that project's runtime interface. The original MIT notice is preserved in LICENSE and embedded in the executable. Boost.JSON uses the Boost Software License in BOOST-LICENSE.txt. Windows launcher additions are maintained here by Karos-archetype. Upstream authors are credited here; this does not imply their involvement in or endorsement of this separate release.

## Installation

Download `BotWLauncher.exe` from Releases and place it beside an existing compatible Windows native runtime named `BotWWindowsHeadless.exe`. That runtime is **not distributed here**. Keep your existing `windows-launch.ini` if you already use this Windows port. Select your own base-game and update folders. New installations default the user-data folder to `%LOCALAPPDATA%/BotWRecomp/user`; the runtime must support this launcher's settings interface. No game data is downloaded by the launcher.

## Settings

- Display mode, window and internal render resolutions, VSync, volume/mute, and cached-effect preparation.
- FPS limits: 30, 60, 90, Unlimited. High caps remain experimental and are not performance guarantees.
- Game language: English, Français, Español for USA game data. The matching updated runtime must read `language=1`, `3`, or `4` from windows-launch.ini and return it through `cafe.language`. Updating this launcher alone cannot add language support to an older runtime. The local development runtime has been updated separately; no runtime patch or game executable is shipped in this release.
- FSR 1 spatial upscaling remains available. **FSR 3.1 is displayed as unavailable**, and selecting it explains the missing gameplay integration. It is not enabled by this release. DLSS and frame generation are not implemented.

## GitHub launcher updates

The launcher checks this repository's public release list at startup; `Check for updates` checks again. `Include pre-releases` defaults on for this preview channel. A newer compatible release enables `Update to <version>`. The updater downloads only the exact `BotWLauncher.exe` asset from the configured repository, checks its size and GitHub's SHA-256 digest, waits for the current launcher to close, and replaces only that executable. `BotWLauncher.previous.exe` preserves the prior launcher. Settings, game files, the runtime and saves are not updated. Internet access is required for release checks; GitHub rate limits and download failures leave the current launcher in place. This is an unsigned experimental binary.

Maintainers: use increasing tags such as `v0.1.0-preview.2`, `v0.1.0-rc.1`, or `v0.1.0`; keep the embedded version in launcher_update.h in sync. Attach `BotWLauncher.exe` directly to the release. A ZIP-only release or a missing SHA-256 digest will not enable automatic updates. The updater supports alpha, beta, preview and rc versions with a numeric suffix. Only the first 100 releases are checked.

## Build the launcher without game files

Use CMake 3.24+, Ninja, a 64-bit LLVM-MinGW compiler, and Boost headers (tested with Boost 1.86). No Vulkan SDK, generated game source, ROM, or RPX is needed for this launcher build.

```powershell
cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DBOOST_ROOT=C:/path/to/boost_1_86_0 -DCMAKE_BUILD_TYPE=Release
cmake --build build --target BotWLauncher launcher_settings_tests launcher_update_tests
ctest --test-dir build --output-on-failure
```

The settings and release-policy tests cover language persistence, invalid settings, pre-release/version ordering, drafts, malformed metadata, repository/asset URL pinning and required SHA-256 digests. The actual update helper was also exercised against a private replacement executable: it waited for the old process, retained a backup, launched the replacement, and preserved settings/save sentinels. Longer gameplay testing remains outside this launcher-only release.
