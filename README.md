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

## Build and Run (Terminal)

From the project root:

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

The executable is written to `bin/` in the project root, so run it from the build folder like this:

```bash
../bin/opengl_starter
```

## Notes

- `build/` is a generated folder and should not be checked into Git.
- `bin/` is the output folder for compiled executables.
- `.gitignore` already ignores `build/`, `bin/`, and `.vscode/`.
