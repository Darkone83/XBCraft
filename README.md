# XbCraft

A port of the classic voxel sandbox game [Craft](https://github.com/fogleman/Craft) to the original Microsoft Xbox, built by **Team Resurgent** / **Darkone83**.

<div align=center>

<img src="https://github.com/Darkone83/XBCraft/blob/main/img/main.jpg" width=400><img src="https://github.com/Darkone83/XBCraft/blob/main/img/game.jpg" width=400>

</div>

<div align=center>

<img src="https://github.com/Darkone83/XBCraft/blob/main/img/Darkone83.png">

</div>

---

## What is this?

XbCraft brings the simple joy of block building and exploration to original Xbox hardware. Dig, place, and explore an infinite procedurally generated world — entirely offline, entirely on original hardware, no emulation required.

This is a passion project for the original Xbox homebrew community. If you own a softmodded Xbox and want something new to play, this is for you.

---

## Features

- Procedurally generated worlds with terrain, trees, plants, and clouds
- First-person block placement and destruction
- Day/night cycle with dynamic sky and fog
- Save and load your world — your progress persists between sessions
- Fly mode for creative exploration
- In-game settings: toggle plants, trees, and clouds; adjust view distance
- Runs at 60fps at view distance 2 on all original Xbox hardware
- Extended view distance (up to 8 chunks) on 128MB modified consoles
- Full controller support

---

## Requirements

- Original Xbox (retail or debug kit)
- Softmodded with a custom dashboard (Rocky5's Softmod, EvoX, UnleashX, or similar)
- At least 50MB free space on the Xbox HDD

> XbCraft does **not** run on Xbox 360 backwards compatibility. Original hardware only.

---

## Installation

1. Download the latest release from the [Releases](../../releases) page
2. Extract the zip — you'll get an `XbCraft` folder
3. Copy the `XbCraft` folder to your Xbox HDD using FTP or a USB transfer tool
   - Suggested path: `E:\Apps\XbCraft\` or `F:\Apps\XbCraft\`
4. Launch from your dashboard by browsing to `default.xbe`

---

## Controls

| Button | Action |
|---|---|
| Left Stick | Move / Strafe |
| Right Stick | Look |
| A | Jump / Ascend (fly mode) |
| B | Toggle fly mode |
| LT | Break block |
| RT | Place block |
| D-Pad Left / Right | Cycle held block |
| L3 (click) | Descend (fly mode) |
| R3 (click) | Zoom |
| START | Pause menu |

---

## Settings

From the main menu or pause menu, open **Settings** to adjust:

- **Plants** — toggle flowers and tall grass
- **Trees** — toggle tree and leaf rendering
- **Clouds** — toggle cloud blocks
- **View Distance** — 2 to 5 chunks (64MB consoles) or 2 to 8 chunks (128MB consoles)

Settings are saved automatically.

---

## Save System

XbCraft saves your world and player position to the Xbox HDD. Your world persists between sessions. Select **Load Game** from the main menu to resume where you left off, or **New Game** to start fresh (this will delete your existing world).

---

## Building from Source

XbCraft is built with the **Xbox Development Kit (XDK)** using Visual Studio 2003 targeting the RXDK runtime. It is written in C/C++ with no external runtime dependencies beyond the XDK and D3DX8.

### Dependencies
- Microsoft Xbox XDK (or compatible RXDK headers)
- Visual Studio 2003 or compatible MSVC toolchain
- D3DX8 (included with XDK)

### Project structure

```
XbCraft/
├── main.cpp          — Entry point, game state machine, main loop
├── chunks.cpp/h      — Chunk mesh generation and rendering
├── chunkcache.cpp/h  — World streaming and HDD save system
├── menu.cpp/h        — All menu screens
├── world.cpp/h       — Terrain and world generation
├── cube.cpp/h        — Block geometry and face building
├── matrix.cpp/h      — Math (projection, view, item matrix)
├── font.cpp/h        — Bitmap font renderer
├── render.cpp/h      — D3D8 device and frame management
├── audio.cpp/h       — Music and XMV video playback
├── input.cpp/h       — Xbox controller input
└── lodepng.c/h       — PNG texture loading (no STL, C-compatible)
```

Build the project by opening `XbCraft.vcxproj` in Visual Studio, selecting the Xbox target, and building. The output `default.xbe` and accompanying media files need to be deployed together.

---

## Credits

**Development**: Darkone83 / Team Resurgent  
**Original Craft**: [Michael Fogleman](https://github.com/fogleman/Craft)  
**Lodepng**: Lode Vandevenne  

Team Resurgent builds tools and homebrew for original Xbox hardware. Find more of our work at the [Xbox-Scene community](https://www.xbox-scene.info).

---

## License

XbCraft is released for personal, non-commercial use. The original Craft game is MIT licensed — see [Craft's repository](https://github.com/fogleman/Craft) for details. XbCraft-specific code is © Team Resurgent / Darkone83.

---

*Built with love for original hardware.*
