cmake_minimum_required(VERSION 3.16)
project(EVChargingSystem VERSION 2.0.0 LANGUAGES CXX)

# EV33 - EV Charging Station Placement and Trip Planning System.
# Everything is C++17 (the vendored GLFW used by the optional desktop app is a C library).
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(EV33_BUILD_TESTS "Build the GoogleTest suites (vendored in third_party/googletest)" ON)
option(EV33_BUILD_BENCHMARKS "Build the benchmark harness and dataset generator" ON)
option(EV33_BUILD_GBENCH "Build the Google Benchmark micro-benchmarks (vendored in third_party/benchmark)" ON)
option(EV33_BUILD_WEB "Build the website server ev33_web" ON)
option(EV33_GUI "Build the desktop app ev33_gui (needs OpenGL; X11 headers on Linux)" OFF)
option(EV33_SANITIZE "Build with AddressSanitizer + UndefinedBehaviorSanitizer (GCC/Clang)" OFF)

if(MSVC)
  set(EV33_WARN /W4 /permissive- /D_CRT_SECURE_NO_WARNINGS)
else()
  set(EV33_WARN -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
endif()

if(EV33_SANITIZE)
  if(MSVC)
    message(WARNING "EV33_SANITIZE: use clang/gcc on Linux or WSL; MSVC only offers /fsanitize=address")
    add_compile_options(/fsanitize=address)
  else()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined)
  endif()
endif()

# ------------------------------------------------------------------ core library
file(GLOB_RECURSE EV33_CORE_SRC CONFIGURE_DEPENDS
  src/core/*.cpp src/algorithms/*.cpp src/graph/*.cpp src/models/*.cpp src/io/*.cpp
  src/planner/*.cpp src/placement/*.cpp src/operations/*.cpp src/analysis/*.cpp src/datagen/*.cpp)
add_library(ev33_core STATIC ${EV33_CORE_SRC})
target_include_directories(ev33_core PUBLIC include)
target_compile_options(ev33_core PRIVATE ${EV33_WARN})

# ------------------------------------------------------------------ applications
add_executable(ev33 src/app/main.cpp src/app/commands.cpp)
target_link_libraries(ev33 PRIVATE ev33_core)
target_compile_options(ev33 PRIVATE ${EV33_WARN})

add_executable(ev33_gen tools/ev33_gen.cpp)
target_link_libraries(ev33_gen PRIVATE ev33_core)
target_compile_options(ev33_gen PRIVATE ${EV33_WARN})

# ------------------------------------------------------------------ tests (GoogleTest)
if(EV33_BUILD_TESTS)
  enable_testing()
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)   # MSVC: match the project's runtime
  add_subdirectory(third_party/googletest EXCLUDE_FROM_ALL)
  add_subdirectory(tests)
endif()

# ------------------------------------------------------------------ benchmarks
if(EV33_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

# ------------------------------------------------------------------ front ends
# Shared layer of the website and the desktop app: engine adapter, number formatting, report text.
if(EV33_BUILD_WEB OR EV33_GUI)
  add_library(ev33_shared STATIC shared/engine.cpp shared/fmt.cpp shared/content.cpp)
  target_include_directories(ev33_shared PUBLIC shared)
  target_link_libraries(ev33_shared PUBLIC ev33_core)
  target_compile_options(ev33_shared PRIVATE ${EV33_WARN})
endif()

# Website server (Render): pages are generated in C++, no JavaScript.
if(EV33_BUILD_WEB)
  find_package(Threads REQUIRED)
  add_executable(ev33_web server/web.cpp)
  target_link_libraries(ev33_web PRIVATE ev33_shared Threads::Threads)
  target_compile_definitions(ev33_web PRIVATE EV33_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_compile_options(ev33_web PRIVATE -Wall -Wextra)
  if(WIN32)
    target_link_libraries(ev33_web PRIVATE ws2_32)
  endif()
endif()

# Desktop app: Dear ImGui + ImPlot on GLFW/OpenGL (vendored in third_party/).
if(EV33_GUI)
  if(APPLE)
    message(FATAL_ERROR "ev33_gui is built on Windows and Linux (the vendored GLFW has no macOS backend)")
  endif()
  set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
  add_subdirectory(third_party/glfw EXCLUDE_FROM_ALL)
  set(IMGUI third_party/imgui)
  add_library(imgui_implot STATIC
    ${IMGUI}/imgui.cpp ${IMGUI}/imgui_draw.cpp ${IMGUI}/imgui_tables.cpp ${IMGUI}/imgui_widgets.cpp
    ${IMGUI}/backends/imgui_impl_glfw.cpp ${IMGUI}/backends/imgui_impl_opengl3.cpp
    third_party/implot/implot.cpp third_party/implot/implot_items.cpp)
  target_include_directories(imgui_implot PUBLIC ${IMGUI} ${IMGUI}/backends third_party/implot)
  target_compile_definitions(imgui_implot PUBLIC GLFW_INCLUDE_NONE)
  target_link_libraries(imgui_implot PUBLIC glfw ${CMAKE_DL_LIBS})
  add_executable(ev33_gui gui/main.cpp gui/map.cpp gui/ui.cpp gui/views.cpp)
  target_link_libraries(ev33_gui PRIVATE ev33_shared imgui_implot)
  target_compile_definitions(ev33_gui PRIVATE EV33_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_compile_options(ev33_gui PRIVATE -Wall -Wextra)
  if(WIN32)
    set_target_properties(ev33_gui PROPERTIES WIN32_EXECUTABLE ON)   # a normal window, no console
  endif()
endif()
