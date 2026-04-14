# OpenGL Starter (CMake + GLFW)

Minimal OpenGL starter project for Windows using:

- CMake
- C++17
- GLFW (downloaded automatically by CMake)
- System OpenGL (`OpenGL::GL`)

## Prerequisites

Install these tools first:

- CMake 3.20+
- A C++ compiler (Visual Studio Build Tools, Visual Studio, or MinGW)

## Build and Run (VS Code Tasks)

1. Open the project in VS Code.
2. Run task: `CMake: Build`
3. Run task: `CMake: Run`

## Build and Run (Terminal)

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
.
\build\opengl_starter.exe
```

If your generator is multi-config (for example Visual Studio), run:

```powershell
.\build\Debug\opengl_starter.exe
```
