# 🟢 Knobby Life Counter 

This is a non-commercial hobby project. Contributors are not associated with any company.

> [!Note]
> Full disclosure: this was programmed with the help of generative AI

## ✨ Features

Features/intended use:
- Life counter for Magic: the Gathering or other TCGs
- Life tracking from -999 to 999 with delta being shown as preview for 4 seconds
- Support for 1 to 4 players
- Commander damage for up to 4 players, damage to all players
- Game timer (hours:minutes) and turn counter
- Brightness and battery guesstimate (WIP)
- D20 dice roll
- Event log
- Device name, asked once on the first boot and editable from Settings
- Factory reset from Settings (hold to confirm), and over-the-air updates with a
  test channel you alone point at specific devices - see below

## 🛠️ Installation

Go to https://knobby-mtg.github.io/knobby-mtg-life-counter/ to install the latest release with a compatible browser.

> [!Warning]
> Installation is at your own risk, we take no responsiblity for failed installations or devices.

## 📦 Releases and test builds

Devices update over WiFi from this repo's GitHub Pages site. There are two
channels, and a device is on exactly one of them:

| Channel | Where it is published | Who gets it |
|---|---|---|
| `stable` | site root | every device |
| `test` | site `/test/` | only the devices listed in `ota.json` |

The site is the `gh-pages` branch, served by Pages directly from it: each
release adds its channel's files to what is already there instead of replacing
the site, so publishing a beta cannot take the stable binaries down with it.

`ota.json`, at the root of the site, is the whole routing table:

```json
{
  "version": "v1.4.0",           "bin": "knobby.ino.bin",
  "test_version": "v1.5.0-rc1",  "test_bin": "test/knobby.ino.bin",
  "test_devices": "A1B2C3,DE45F6"
}
```

A **device id** is the six hex digits shown on the device's own
Settings → Updates screen, next to its name. It comes from the MAC, so it
survives a rename and a factory reset - which is why it, and not the name, is
what the list targets.

**To publish a build:** push a tag. A plain version (`v0.7.2`) goes to
**stable**, i.e. to every device; a suffixed one (`v0.7.3-beta1`, `v0.8.0-rc1`)
goes to the **test** channel and reaches only the devices on the list. The same
suffix marks the GitHub Release a prerelease. Running the *Release Firmware*
workflow by hand overrides that: pick the channel, the version string, and
optionally the tester list in one go.

**To change who receives the current test build:** run the *Choose who gets test
builds* workflow with a comma-separated list of ids. It edits one field and
pushes it - no rebuild, no new release, live a minute or two later. Removing a device from the list
puts it back on the stable build at its next check, so that is also how a test
build is rolled back.

There is deliberately **no setting on the device** to opt into test builds: the
list is the only way in, so a tester cannot put their own unit on a beta and you
always know exactly who is running what. The device's Updates screen only
*reports* which channel it was put on.

## 🚀 Getting Started

Swipe inward from any edge on a player screen to open the menu, and swipe down or in from the right edge to close a menu or go back.

For how-to guides and additional documentation, see the [wiki](https://github.com/knobby-mtg/knobby-mtg-life-counter/wiki).

## ⚙️ Hardware

This is the hardware used: JC3636K518 with battery, you can find it on AliExpress.

Specifications:
- 1.8 inch display, resolution 360*360
- Display driver: ST77916
- Touch: CST816
- CPU: 240 MHz
- Platform: ESP32

These boards may also be supported depending on hardware revision:
* https://www.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8
* The JC3636K718 board varient is also unofficially supported (currently experimental).

## 🔧 Building

All build commands run from the repo root:

```bash
# First time: install Arduino cores and libraries
make firmware-deps

# Compile firmware for ESP32-S3
make firmware

# Flash to device (replace port with yours from `arduino-cli board list`)
make firmware-flash PORT=/dev/ttyACM0
```

## 📸 Screenshots

A headless simulator renders the UI without hardware, producing PNG screenshots with a circular display mask matching the physical device.

```bash
# Single screenshot
make screenshot                                    # default main screen
make screenshot ARGS="--screen 4p --life 200,30,40,15 --names Alice,Bob,Charlie,Dave"

# Full test matrix (308 screenshots + index.html gallery)
make generate-matrix
```

Run `sim/knobby_sim --help` for all CLI options. See [sim/screenshots/README.md](sim/screenshots/README.md) for details.

## 🕹️ Simulators

You can test the Knobby interface on your PC or in a browser without hardware. 
Both **`make`** commands and **`./*.sh`** scripts are provided for convenience.

### Web Simulator (WASM) 🌐
Runs the **actual C firmware** compiled to WebAssembly directly in the browser.
- **Run server:** `make sim-web-run` (or `./sim-web.sh`)
- **Build from source:** `make sim-web-build` (or `./compile.sh`)

*Open [http://localhost:8000/sim/index.html](http://localhost:8000/sim/index.html) after starting the server.*

> [!Note]
> A pre-compiled `knobby_web.wasm` is already included. You only need to install Emscripten if you want to recompile from source.

#### Installing Emscripten (for recompilation)
```sh
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh   # or emsdk_env.bat on Windows
```

### PC Simulator (Native) 💻
A native application for local interactive development.
- **Run:** `make sim` (or `./sim.sh`)

**Controls:**
- **Touch:** Left Click
- **Knob:** Mouse Wheel / Left & Right Arrows
- **Swipe:** Up & Down Arrows | L & R keys

## ✅ Testing

Headless unit tests build against the same simulator sources (no hardware, no display) and cover game logic directly: life/preview rules, commander damage, elimination + undo, the damage log, Table Sync's state-merge rules, the battery curve, and translation-table completeness.

```bash
make -C sim test                     # game logic (life, elimination, damage log, Table Sync, ...)
                                     # plus prefs: device name, factory reset, update channel routing
make -C sim test-mem-budget          # regression check: all screens vs. the device's 128KB LVGL pool
make -C sim test-game-state-purity   # game_state.h compiles with zero LVGL dependency
```

See [sim/tests/test_harness.h](sim/tests/test_harness.h) for how a test boots the UI headlessly, and any existing file under `sim/tests/` for the pattern (one binary per file, plain `assert()`).

The game's actual rules live in [knobby/src/entities/game_state.h](knobby/src/entities/game_state.h)/`.c`, with zero LVGL types in the header - `usecases/game.h`/`.c` is the LVGL-facing bridge (color math, the 3 timers that schedule follow-ups) that everything else still includes exactly as before. See the comment at the top of `game_state.h` for the split.

## 🧑‍🤝‍🧑 Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md)
