# CODEBASE_OVERVIEW.md
> Concise reference for AI agents and developers. Describes how the project is structured, how assets are located, and how files are loaded at runtime.

---

## 1. Project Structure Overview

```
Project_OpenGL/
├── CMakeLists.txt              # Build system (CMake 3.20+, C++17)
├── GameLogic.md                # High-level game design notes
├── README.md
├── resources/                  # All runtime assets (copied to build dir by CMake)
│   ├── config.json             # Data-driven game balance configuration
│   ├── ui_sample.png           # UI reference image
│   └── object/                 # All 3D models and textures
│       ├── arena/              # Arena/map (glTF format)
│       ├── mixamo/             # Player character + animation clips (.dae)
│       ├── enemy_spider_leela/ # Enemy model (glTF format)
│       └── staff/              # Weapon model (.glb format)
├── src/                        # All C++ source files
│   ├── main.cpp                # Entry point, game loop, render, UI, GameContext
│   ├── assets.h / assets.cpp   # All asset loading: meshes, textures, animations
│   ├── game.h / game.cpp       # GameState, GameConfig, game logic helpers
│   ├── graphics.h / graphics.cpp # OpenGL draw utilities (render_model, render_arena)
│   ├── math.h / math.cpp       # Vec3, Mat4, transforms
│   └── glad.c                  # OpenGL loader (compiled from includes/)
└── includes/                   # Vendored header-only / static libraries
    ├── glad/                   # GLAD OpenGL loader headers
    ├── GLFW/                   # GLFW window/input headers
    ├── glm/                    # GLM math (available but mostly unused; custom math used)
    ├── assimp/                 # Assimp headers (system Homebrew on macOS)
    ├── stb_image.h             # Image decoding (STB, header-only)
    ├── nlohmann/json.hpp       # JSON parsing (fetched via CMake FetchContent)
    └── freetype/, irrKlang/    # Additional vendored libs (freetype/audio, not fully active)
```

---

## 2. Resource / Asset Directory Layout

All assets live under `resources/` (source tree) and are **post-build copied** by CMake to sit beside the executable in the build output directory.

| Path (relative to `resources/`) | Content |
|---|---|
| `config.json` | Balance config for player, enemy, and upgrades |
| `object/arena/scene.gltf` + `scene.bin` | Arena map geometry + binary buffer |
| `object/arena/textures/` | Arena texture images (PNG) |
| `object/mixamo/Vanguard By T. Choonyung/Vanguard By T. Choonyung.dae` | Player skeletal mesh |
| `object/mixamo/Great Sword Idle.dae` | Idle animation clip |
| `object/mixamo/Standing Walk Forward.dae` | Walk animation clip |
| `object/enemy_spider_leela/Leela.gltf` | Enemy model (self-contained glTF) |
| `object/enemy_spider_leela/Leela_Texture.png` | Enemy texture |
| `object/staff/as-vulcan_scarlet_lance_-_3d_model_stylized.glb` | Weapon model (GLB, embedded textures) |
| `object/cube.obj` | Simple unit cube (utility mesh) |

---

## 3. File Loading Logic in Code

### 3.1 `find_resource()` — Universal Path Resolver

**Defined in:** `src/assets.cpp` (lines 250–267)  
**Declared in:** `src/assets.h`

```cpp
// Searches three candidate roots in order:
std::vector<std::filesystem::path> resource_roots() {
    const std::filesystem::path cwd = std::filesystem::current_path();
    return {
        cwd / "resources",                          // 1. Beside the executable (normal runtime)
        cwd.parent_path() / "resources",            // 2. One directory up (some build layouts)
        std::filesystem::path(APP_SOURCE_DIR) / "resources"  // 3. Source tree (CMake-injected absolute path)
    };
}

std::filesystem::path find_resource(const std::string& relative_path);
```

`APP_SOURCE_DIR` is a compile-time macro injected by CMake:
```cmake
target_compile_definitions(opengl_starter PRIVATE APP_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
```

All call-sites pass a **relative path** rooted at `resources/`:
```cpp
find_resource("config.json")
find_resource("object/arena/scene.gltf")
find_resource("object/mixamo/Great Sword Idle.dae")
```

### 3.2 Asset Loading Functions (`assets.cpp`)

| Function | Input Format | Usage |
|---|---|---|
| `load_arena_mesh()` | glTF 2.0 (`.gltf` + `.bin`) | Arena map — custom JSON parser, no Assimp |
| `load_static_model_mesh()` | Any (`.dae`, `.glb`, `.gltf`, `.obj`) via Assimp | Enemy, staff, static props |
| `load_animated_model()` | `.dae` via Assimp | Player character (skeletal mesh + nodes + bones) |
| `load_animation_clip()` | `.dae` via Assimp | Separate animation file appended to `AnimatedModel::clips` |
| `upload_arena_textures()` | PNG (file path or embedded) via stb_image | Uploads textures to GPU after mesh load |
| `free_arena_textures()` | — | Deletes GL texture objects |

### 3.3 Texture Resolution

- **glTF arena meshes**: image URIs are resolved as `gltf_path.parent_path() / uri` (same directory as the `.gltf` file).
- **Assimp models**: texture paths are extracted from material data, resolved as `model_path.parent_path() / relative_texture_path`.
- **Embedded textures** (GLB format): detected when texture path starts with `*`; decoded in-memory via `stbi_load_from_memory`.
- **Uploading**: `stbi_load()` decodes to RGBA pixels, then `glTexImage2D()` uploads to GPU. `stbi_set_flip_vertically_on_load(1)` is set globally.

### 3.4 Game Config Loading (`game.cpp` / `main.cpp`)

```cpp
// On startup and on hot-reload (R key while game_over):
const auto config_path = find_resource("config.json");
game.config = load_game_config(config_path.string());
```

`load_game_config()` parses `resources/config.json` using `nlohmann::json`. All fields have C++ defaults in `GameConfig` as fallbacks if keys are absent.

---

## 4. Path Conventions

| Convention | Description |
|---|---|
| All paths are **relative to `resources/`** | Never hardcoded absolute paths in C++ call-sites |
| `find_resource()` is the **single entry point** for locating any file | Eliminates scattered path logic |
| Texture paths in 3D files are **relative to the model's own directory** | glTF/Assimp both resolve this way |
| Embedded textures (GLB) use `*N` sentinel strings | Handled transparently in `get_material_texture()` |
| CMake copies `resources/` beside the executable post-build | Runtime CWD search always finds assets |
| `APP_SOURCE_DIR` macro provides a compile-time fallback | Enables in-source-tree runs (e.g. CLion, direct exec) |
| Skeleton/animation files are separate `.dae` files, not embedded | `load_animation_clip()` appends clips to an existing `AnimatedModel` |

---

## 5. Key Files Responsible for Resource Loading

| File | Role |
|---|---|
| `src/assets.h` | Public API: `find_resource()`, `load_arena_mesh()`, `load_animated_model()`, `load_static_model_mesh()`, `upload_arena_textures()` |
| `src/assets.cpp` | Full implementation: path resolution, glTF parsing, Assimp import, stb_image decode, OpenGL texture upload |
| `src/game.h` | `GameConfig` struct + `load_game_config()` declaration |
| `src/game.cpp` | `load_game_config()` — parses `config.json` with nlohmann::json |
| `src/main.cpp` | Orchestration: calls `find_resource()` for each asset, calls loaders, wires all assets into `GameContext` |
| `CMakeLists.txt` | Sets `APP_SOURCE_DIR` compile definition; post-build copies `resources/` to executable directory |
| `resources/config.json` | Runtime-editable game balance data (hot-reloaded on R+restart) |

---

## 6. Runtime Asset Loading Sequence (startup)

```
main()
  │
  ├─ find_resource("object/arena/scene.gltf")   → load_arena_mesh()
  │     └─ upload_arena_textures()              → stbi_load() → glTexImage2D()
  │
  ├─ find_resource("object/mixamo/.../Vanguard...dae") → load_animated_model()
  │     ├─ find_resource("object/mixamo/Great Sword Idle.dae") → load_animation_clip()
  │     └─ find_resource("object/mixamo/Standing Walk Forward.dae") → load_animation_clip()
  │
  ├─ find_resource("object/enemy_spider_leela/Leela.gltf") → load_static_model_mesh()
  │
  ├─ find_resource("object/staff/...glb")       → load_static_model_mesh()
  │
  └─ find_resource("config.json")               → load_game_config()
        └─ GameState.config populated
```

> **Fallback policy**: every asset is optional except the arena. Missing models degrade gracefully to colored bounding-box placeholders (`draw_box()`). Missing config falls back to C++ struct defaults.
