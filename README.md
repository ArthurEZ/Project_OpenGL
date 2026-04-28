# 3D Cyber Survivor Arena

A fast-paced OpenGL-based top-down survival shooter with dynamic difficulty scaling, real-time enemy AI, and a progression system. Survive endless waves of enemies while managing health, upgrades, and increasingly challenging spawning mechanics.

## Game Sample

### Video

https://github.com/user-attachments/assets/4040a283-b96d-4a0e-a971-c1f0df1f4148

### Screenshot

<p align="center">
  <img src="https://github.com/user-attachments/assets/0159e65f-e452-482d-80a6-26e14b0cc583" width="48%" />
  <img src="https://github.com/user-attachments/assets/44bdc30c-1ca9-4151-89ed-3b8f838b623e" width="48%" />
</p>

## Idea

In this arena-based survival game, you control a player character on a fixed circular map and must defeat an infinite stream of enemies. Your weapon auto-targets the nearest enemy within range, encouraging smart positioning and kiting. As you survive longer, the difficulty scales dynamically—enemies spawn faster, move quicker, and gain more health. Collect experience from kills, level up, and choose between three randomized upgrades each time you gain a level. The game ends when your health reaches zero; your goal is to maximize survival time and kill count.

## Controls

| Key / Input   | Action                                                |
| ------------- | ----------------------------------------------------- |
| **W**         | Move Forward                                          |
| **A**         | Move Left                                             |
| **S**         | Move Backward                                         |
| **D**         | Move Right                                            |
| **Auto-Fire** | Continuously fires at nearest enemy (no key required) |
| **R**         | Restart Game (when game over)                         |
| **ESC**       | Exit Game                                             |
| **Mouse**     | Interact with UI (level-up menu, restart button)      |

## Technical stuff

### Rendering / Core Tech

- **Framework**: OpenGL 4.1+ (Core Profile) via GLAD loader
- **Window / Input**: GLFW 3.x
- **Build System**: CMake 3.20+
- **Language**: C++17
- **Rendering Approach**: Immediate-mode deferred transform pipeline
  - Per-frame world matrix computation
  - Vertex data (positions, UVs, bone IDs/weights) streamed to GPU
  - Dynamic texture binding (base color + emissive maps)
  - Alpha blending for transparency
  - Health bar overlays in screen space

### Models / Data / Assets

- **Model Format**: glTF 2.0 (arena), DAE/GLB via Assimp (characters/enemies/props)
- **Texture Format**: PNG via stb_image
- **Configuration**: JSON (nlohmann/json) for game balance data
- **Asset Loading**:
  - `find_resource()` provides a unified path resolver with compile-time fallback (APP_SOURCE_DIR macro)
  - All assets bundled in `resources/` subdirectory
  - CMake post-build copies assets beside executable
  - Custom glTF JSON parser for arena mesh (no Assimp overhead)
  - Assimp for animated/static models (skeletal data, node hierarchies, materials)
  - Embedded textures in GLB format detected and decoded in-memory
- **Asset Categories**:
  - Arena: `object/arena/scene.gltf` + textures
  - Player: `object/mixamo/Vanguard.dae` (skeletal) + animation clips (`.dae` files)
  - Enemy: `object/enemy_spider_leela/Leela.gltf` (static)
  - Weapon: `object/staff/scarlet_lance.glb` (static, embedded textures)

### Core Systems

**Game State & Config**

- `GameState` struct: holds player position/velocity, health, experience, enemies, projectiles, timers
- `GameConfig`: data-driven balance parameters (HP, speed, damage, spawn rates, upgrade values)
- Hot-reload on restart: config reloaded from JSON

**Game Loop Execution Order**

1. **Input** — WASD movement input captured, ESC/R for control
2. **Movement** — Player velocity normalized (prevent diagonal boost), applied to position
3. **Spawn Check** — Dynamic spawn interval: `max(0.25, 0.9 - time * 0.006)` scales with survival time
   - Hard cap: 150 enemies active
   - Spawn location: arena perimeter
4. **Auto-Attack** — Fires at nearest enemy within range (360° FOV), only if one exists
5. **Enemy AI** — Direct pathfinding: each enemy moves toward player using `normalize(player_pos - enemy_pos)`
6. **Projectile Physics** — Linear trajectory, fixed speed/lifetime, destroyed on expire
7. **Collision Resolution**:
   - **Projectile ↔ Enemy**: Circle-to-circle check, single-hit (projectile destroyed, enemy damaged)
   - **Enemy ↔ Player**: Continuous contact damage
   - **Enemy ↔ Enemy**: Soft-body separation (push forces prevent stacking)
8. **State Update** — Timers decrement, experience tracked, level-ups triggered

**Difficulty Scaling (Linear over Time)**

- Spawn Interval: Decreases per formula (Section 3 above)
- Enemy Speed: `base_speed + (survival_time * 0.0015)`, capped at 6 u/s
- Enemy HP: `base_hp + (survival_time * 0.4)`

**Combat & Weapons**

- Projectiles: Fixed speed (14 u/s base, upgradeable), lifetime ~1.2s, non-piercing
- Player Attack: 0.18s fire rate (configurable), damage 30 base (upgradeable)
- Nearest Enemy Logic: O(n) brute force, optimized with spatial grid for collision checks

**Progression System**

- **Experience**: Gain from enemy kills, accumulated in `player_exp`
- **Level-Up**: When `player_exp >= exp_to_next_level`, trigger level-up, roll 3 random options
- **Upgrades** (selected at level-up):
  - Max HP boost
  - Heal (restore current HP)
  - Move speed increase
  - Projectile speed increase
  - Damage increase

**Spatial Optimization**

- `SpatialGrid` (main.cpp): Hash-based spatial partitioning (cell_size = 1.5 units)
- Reduces collision check count from O(n²) to O(n)
- Enemies inserted into grid each frame, queried for collision ranges

### Camera / UI

**Camera System**

- Fixed isometric-style tilt: pitch ≈ 35°, yaw locked to player heading
- Position strictly locked to player: `camera_pos = player_pos + offset`
- Zoom distance: target 16 units, smoothly lerped each frame
- View matrix: computed from position + look-at point

**UI Rendering**

- **Bitmap Font**: Hand-coded 5×7 glyph patterns for alphanumeric + punctuation
- **HUD Elements**:
  - Survival Time (top-left)
  - Kill Count (top-left)
  - Player Health Bar (top-left, screen-space)
  - Level / Experience (bottom info)
  - Level-Up Menu: 3 upgrade options with descriptions, mouse-clickable buttons
  - Death Screen: Shows final stats, restart prompt
- **Health Bars**: World-space for enemies, screen-space overlay for player
- **Text Rendering**: `draw_text_screen()` uses immediate-mode GL primitives (QUADS per glyph)

### Effects / Optimization

**Visual Effects**

- Health bars with color gradient (red ↔ green based on HP ratio)
- Emissive textures supported in material pipeline (for glow/neon effect)
- Alpha blending for transparent geometry

**Performance Optimizations**

- Spatial grid for O(1) collision range queries
- Vertex data computed once per model per frame
- Texture atlasing via material indices
- Frustum culling (enemies/projectiles outside arena radius deleted)
- Model instancing avoided (single-draw per model type, transform per entity)

**Animation System**

- Skeletal animation supported via Assimp bone hierarchy + animation clips
- Linear interpolation between keyframes (position, rotation, scale)
- Clip blending: animations can be loaded and switched (e.g., idle ↔ walk)
- Animation playback synchronized to gameplay (e.g., enemy walk speed matched to AI movement)

### Game Loop / Runtime Logic

**Main Loop Structure** (`main.cpp`)

```
while (!glfwWindowShouldClose(window)) {
  UpdateInput();
  if (!game_over) {
    ApplyMovement();
    SpawnEnemies();
    UpdatePlayerShooting();
    UpdateEnemyAI();
    UpdateProjectiles();
    CheckCollisions();
    UpdateGameState();
  }
  Render();
  Swap Buffers;
}
```

**State Handling**

- `game_over` flag: when `player_hp <= 0`, freeze all logic, render death UI
- `survival_time`: continuously incremented (deltaTime), drives difficulty scaling
- `pending_levelups` / `levelup_options`: FSM for level-up menu (blocks input until choice made)
- `shoot_timer` / `spawn_timer`: countdown timers for fire rate and spawn intervals

**Timing**

- `glfwGetTime()` for frame-to-frame deltaTime
- All movement, physics, and timers use deltaTime for frame-rate independence
- Animation time advanced independently per clip

## Build and Run

### Prerequisites

- CMake 3.20+
- C++17 compiler (Clang, GCC, or MSVC)
- macOS / Linux / Windows (tested on macOS)
- OpenGL 4.1+ capable hardware

### Build

```bash
cd /path/to/Project_OpenGL
mkdir -p build
cd build
cmake ..
cmake --build . --config Release
```

The executable will be at: `build/opengl_starter`

### Run

```bash

./build/opengl_starter
```

Assets are automatically located via the `find_resource()` resolver (checks `resources/` relative to executable, parent directory, and source tree).

## Project Structure

```
Project_OpenGL/
├── CMakeLists.txt              # Build config, FetchContent (nlohmann_json), post-build copy
├── GameLogic.md                # Design spec (mechanics, difficulty curve)
├── CODEBASE_OVERVIEW.md        # Asset loading architecture
├── README.md                   # This file
├── resources/                  # All runtime assets
│   ├── config.json             # Balance configuration (hot-reloaded on restart)
│   └── object/                 # 3D models & textures
│       ├── arena/              # Arena map (glTF)
│       ├── mixamo/             # Player character (DAE + animation clips)
│       ├── enemy_spider_leela/  # Enemy model (glTF)
│       └── staff/              # Weapon model (GLB)
├── src/                        # C++ implementation
│   ├── main.cpp                # Entry point, game loop, rendering, UI
│   ├── game.h/cpp              # GameState, GameConfig, game logic (spawn, combat, AI)
│   ├── graphics.h/cpp          # OpenGL rendering primitives (render_model, health bars, text)
│   ├── assets.h/cpp            # Asset loading (glTF parser, Assimp wrapper, texture upload)
│   ├── math.h/cpp              # Vec3, Mat4, transforms, utility math
│   └── glad.c                  # OpenGL loader (generated from glad)
└── includes/                   # Vendored headers
    ├── glad/                   # OpenGL 4.1 Core headers
    ├── GLFW/                   # Window & input headers
    ├── glm/                    # (available, mostly unused; custom math preferred)
    ├── assimp/                 # Model loading headers
    ├── stb_image.h             # Image decoding (header-only)
    ├── nlohmann/json.hpp       # JSON (FetchContent'd by CMake)
    └── freetype/, irrKlang/    # Audio/text (available, not fully integrated)
```

## Notes

- `build/` and `bin/` are generated directories and should not be committed to version control.
- `.gitignore` already excludes these folders.
- Config hot-reload: modify `resources/config.json` and restart the game (R key) to apply changes.
- All paths are resolved relative to `resources/`; see `CODEBASE_OVERVIEW.md` for asset loading details.

## Credits

This project uses external 3D assets and animations from the following sources:

- **Lightcycle Arena Rev 02 (TEBG)**
  Source: Sketchfab
  https://sketchfab.com/3d-models/lightcycle-arena-rev-02-tebg-4f52ab3038a94557a3b69e899297b945

- **AS Vulcan Scarlet Lance (Stylized)**
  Source: Sketchfab
  https://sketchfab.com/3d-models/as-vulcan-scarlet-lance-3d-model-stylized-655eb45f8e964f8b8ae3bc453ae58be6

- **Character & Animations (Mixamo)**
  Source: Adobe Mixamo
  https://www.mixamo.com/

- **Animated Mech Pack**
  Source: Quaternius
  https://quaternius.com/packs/animatedmech.html

---

### Licensing Notes

- Sketchfab assets are used under their respective licenses. Please refer to the original model pages for detailed terms and restrictions.
- Mixamo animations are provided by Adobe for free use in projects (subject to Adobe terms).
- Quaternius assets are released under **CC0 (Public Domain)** and are free for both personal and commercial use. ([Quaternius][1])

---

### Disclaimer

All third-party assets remain the property of their respective creators.
This project is for educational purposes and does not claim ownership over any external resources.

[1]: https://quaternius.com/packs/animatedmech.html?utm_source=chatgpt.com "Quaternius • Animated Mech Pack"
