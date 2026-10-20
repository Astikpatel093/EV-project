# Third-party libraries (vendored, unmodified)

The desktop app `ev33_gui` is written in C++ and uses these C/C++ libraries. They are copied
here so the project builds offline with only CMake and a C/C++ compiler.

| Library | Version | Language | Licence | Used for |
|---|---|---|---|---|
| [Dear ImGui](https://github.com/ocornut/imgui) | v1.91.5 | C++ | MIT (`imgui/LICENSE.txt`) | windows, widgets, tables, map drawing |
| [ImPlot](https://github.com/epezent/implot) | v0.16 | C++ | MIT (`implot/LICENSE`) | charts |
| [GLFW](https://github.com/glfw/glfw) | 3.4 | C | zlib (`glfw/LICENSE.md`) | window, input and OpenGL context |

Only the files needed to build are kept. GLFW's docs, examples, tests, its Wayland protocol files and its
macOS backend (Objective-C) are left out, so everything compiled here is C or C++; the app builds on
Windows and on Linux (X11, which also runs under Wayland through XWayland).
Fonts in `assets/fonts/` are IBM Plex Sans and Mono, SIL Open Font License (`OFL-IBM-Plex.txt`).
