<p align="center"><img src="docs/logo.png" width="160" alt="KlipIt"></p>

# KlipIt

A gothic game-clipping app for Windows, in the spirit of Medal and SteelSeries Moments.

- **Instant replay.** KlipIt quietly keeps your last moments in memory. Press **F8** and they're saved as a clip.
- **Automatic game detection.** Start a game and KlipIt hooks it by itself: it records only the game, never Discord, browsers or other windows.
- **Clip length 1 second to 20 minutes** (presets from 15 s to 20 min), with pre-event and post-event timing. No clip is ever longer than 20 minutes.
- **Full-session recording**, saved as back-to-back parts of up to 20 minutes so nothing is cut off.
- **Game-only audio, voice only, or both** (as separate tracks).
- **Screenshots** (F10), **automatic highlights** when you get loud, **per-game capture profiles**.
- **Library** sorted by game, favourites, and a **video editor** that saves trims as new clips and splits long videos into 20-minute parts.
- Black theme by default, plus Dark grey, White, Blue and Purple.
- Low impact: capture and encoding run on your graphics card (NVIDIA, AMD or Intel), up to 1440p (2K).

## Get it

Download **KlipIt-x.y.z-Setup.exe** from the latest build under **Actions → Windows build → Artifacts**
(or from **Releases** once a version is tagged) and run it.

## How it's built

Native Windows app: C++20, Qt 6 for the interface, and the open-source capture engine from OBS Studio
(libobs, used only for capture and encoding; KlipIt never streams). The installer is made with Inno Setup.
GitHub Actions builds everything: see `.github/workflows/windows-build.yml`.

Licensed under the GNU GPL v2 or later (see `LICENSE`).
