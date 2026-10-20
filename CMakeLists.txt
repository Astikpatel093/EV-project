cmake_minimum_required(VERSION 3.16)
project(ev33 VERSION 1.0.0 LANGUAGES C CXX)

# Engine, CLI, benchmark runner, generator and tests: C11.
# Desktop app (ev33_gui): C++17 with Dear ImGui + ImPlot on GLFW/OpenGL (vendored in third_party/).
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
option(EV33_GUI "Build the desktop app ev33_gui (needs OpenGL; X11 dev headers on Linux)" ON)
if(APPLE AND EV33_GUI)
  # the vendored GLFW keeps only its C backends (Windows, X11); its macOS backend is Objective-C
  message(STATUS "ev33_gui is built on Windows and Linux; on macOS only the C programs are built.")
  set(EV33_GUI OFF)
endif()

set(EV33_WARN $<IF:$<C_COMPILER_ID:MSVC>,/W4 /D_CRT_SECURE_NO_WARNINGS,-Wall -Wextra -pedantic>)

file(GLOB EV33_LIB_SRC src/lib/*.c src/domain/*.c)
add_library(ev33lib STATIC ${EV33_LIB_SRC})
target_include_directories(ev33lib PUBLIC include)
target_compile_options(ev33lib PRIVATE ${EV33_WARN})
if(NOT MSVC)
  target_link_libraries(ev33lib PUBLIC m)
endif()

foreach(t ev33 ev33_bench ev33_gen ev33_tests)
  if(t STREQUAL "ev33")
    add_executable(ev33 src/app/main.c)
  elseif(t STREQUAL "ev33_bench")
    add_executable(ev33_bench src/app/bench.c)
  elseif(t STREQUAL "ev33_gen")
    add_executable(ev33_gen tools/ev33_gen.c)
  else()
    add_executable(ev33_tests tests/test_main.c)
  endif()
  target_link_libraries(${t} PRIVATE ev33lib)
  target_compile_options(${t} PRIVATE ${EV33_WARN})
endforeach()

# C++ layer shared by the desktop app and the web server: engine wrapper, formatting, report text
add_library(ev33_shared STATIC shared/engine.cpp shared/fmt.cpp shared/content.cpp)
target_include_directories(ev33_shared PUBLIC shared)
target_link_libraries(ev33_shared PUBLIC ev33lib)

# Web server for hosting (Render): pages are generated in C++, no JavaScript
add_executable(ev33_web server/web.cpp)
target_link_libraries(ev33_web PRIVATE ev33_shared)
target_compile_definitions(ev33_web PRIVATE EV33_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
if(WIN32)
  target_link_libraries(ev33_web PRIVATE ws2_32)
endif()

enable_testing()
add_test(NAME ev33_tests COMMAND ev33_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

if(EV33_GUI)
  # GLFW: X11 only on Linux (works under Wayland through XWayland); no docs, tests or examples
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
  if(MSVC)
    target_compile_options(ev33_gui PRIVATE /W4)
  else()
    target_compile_options(ev33_gui PRIVATE -Wall -Wextra)
  endif()
  if(WIN32)
    # a normal window, no console; main() stays the entry point
    set_target_properties(ev33_gui PROPERTIES WIN32_EXECUTABLE ON)
    if(MSVC)
      target_link_options(ev33_gui PRIVATE /ENTRY:mainCRTStartup)
    endif()
  endif()
endif()
