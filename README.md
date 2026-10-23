# BotWRecomp Windows — portable setup

Experimental Windows x64 pre-release **v0.2.0-preview.2**. The launcher supports resizing, maximizing and fullscreen (F11; Escape to leave fullscreen).

Download **[BotWRecomp-Windows-x64.zip](https://github.com/Karos-archetype/BotWRecomp-Windows-Launcher/releases/download/v0.2.0-preview.2/BotWRecomp-Windows-x64.zip)** from [Releases](https://github.com/Karos-archetype/BotWRecomp-Windows-Launcher/releases). Extract the whole ZIP to a writable folder. No separate Python, compiler, Cemu installation or Vulkan SDK is needed.

## First launch

1. Open **BotWLauncher.exe**.
2. Select your own extracted Wii U Breath of the Wild **base-game** and **USA v208 update** folders. Each contains `code/U-King.rpx` and `content/`. A merged, updated dump also works: select it as the base and leave Update blank.
3. Choose your settings and click **Set up and Play**. The bundled tools prepare the game locally, then start it. This first preparation is substantial and can take many minutes; the launcher shows progress. Closing the launcher cancels preparation, and a later attempt reuses completed compilation work.

After setup, open **Play Breath of the Wild.exe** for direct play with the settings last saved in the launcher. Keep that shortcut executable beside BotWLauncher.exe. You can right-click it and create a desktop shortcut. If setup is missing, direct play opens the launcher to finish it.

Keep the entire extracted folder, including `support/`. Do not run the launcher from inside the ZIP. Old launcher-only users should extract this complete package into a new folder; copying just the new launcher is insufficient.

## Bring your own game

No game, update, DLC, keys, Nintendo assets, generated game code, shader caches or saves are included or downloaded. The package contains the host runtime, recompiler, compiler, Python runtime, AMD FSR dependency, source and notices. It builds the game component **on your computer from your dump**. The locally built game executable is not part of the release.

This preview supports the USA v208 executable with SHA-256:

```text
ba58da5b95ce929e005d058ceb08b9b2788d1ab2bbc8a6c189bbadca0bb34d30
```

Other regions/revisions and encrypted WUD/WUX containers are not accepted by this setup. Use an extracted dump from your own Wii U. DLC is not part of this preview's setup.

## Settings and saves

- Display and internal resolution, window/fullscreen mode, VSync, sound volume/mute and game language (English, French, Spanish).
- FPS caps: **30, 60, 90, Unlimited**. Higher caps are experimental, not performance guarantees; some cutscenes remain capped.
- Bicubic, bilinear, FSR 1 and experimental **FSR 3.1 temporal upscaling**. FSR 3.1 includes animated-object vectors and separate gameplay HUD composition. Transparent effects and unsupported passes still use reactive rejection.
- **DLSS and frame generation are not implemented.**

Settings are saved in `windows-launch.ini` beside the launcher. New profiles store saves, caches and local preparation output in `%LOCALAPPDATA%/BotWRecomp/user`. Existing profiles keep their configured user-data location. Neither updating nor moving the portable package should require moving the game dump or deleting saves.

Windows 10/11 x64, a compatible x64 CPU and a Vulkan-capable GPU with its current vendor driver are required. Allow at least 4 GB of free space for local preparation, in addition to your game files and the extracted package. This is an unsigned, experimental build. It has not been validated across the whole game or all hardware, and existing renderer issues remain.

## Updates

Configured launchers check this repository's releases; **Check for updates** also checks manually. Pre-releases are enabled by default. The updater downloads the complete ZIP, checks GitHub's SHA-256 digest, validates every packaged file and rejects unsafe archive paths. It closes the launcher, replaces managed package files, keeps a backup under `.launcher-updates/`, and reopens the launcher. `windows-launch.ini`, game dumps and save-data folders are preserved. Updated host components may require local preparation again; unchanged generated objects can be reused. First-run preparation itself works offline; release checks/downloads need Internet access.

## Credits and contributors

Original project: **[IshaanKalra2103/BotWRecomp](https://github.com/IshaanKalra2103/BotWRecomp)**, Ishaan Kalra and the original contributors. Windows work is maintained by Karos-archetype. Original authors are credited for their work; this does not imply their participation in or endorsement of this release.

The Vulkan GPU layer derives from **[Cemu](https://github.com/cemu-project/Cemu)** (MPL-2.0). Other components include AMD FidelityFX FSR, SDL, glslang, libavc, Boost, fmt, GLM, zlib, Zstandard, Python and LLVM-MinGW. Notices are included in `licenses/`. The corresponding project source, including the modified MPL-covered files, is included in **Source.zip** inside the portable package and separately on the release.

AI-assisted implementation, debugging, testing and documentation: **[OpenAI Codex](https://openai.com/codex/)**, under the maintainer's direction.

Unofficial project; not affiliated with Nintendo. Breath of the Wild and related marks belong to their respective owners.

## Source and development

The repository root contains the launcher, direct-play helper, offline preparation and package updater sources. They can be built without game data using CMake 3.24+, LLVM-MinGW and Boost headers. The full port source and build files are in Source.zip. Generating or building the game component requires your own supported dump; no generated game source or object files are distributed.
