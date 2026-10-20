# Cross-compile EV33 for 64-bit Windows from Linux with a MinGW-w64 toolchain, e.g.
#   cmake -S . -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DMINGW_ROOT=/opt/xmingw
# On Windows itself, use MSYS2's MinGW-w64 (see README, "Build on Windows") instead of this file.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
if(NOT MINGW_ROOT)
  set(MINGW_ROOT /usr)
endif()
set(CMAKE_C_COMPILER ${MINGW_ROOT}/bin/x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER ${MINGW_ROOT}/bin/x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER ${MINGW_ROOT}/bin/x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH ${MINGW_ROOT}/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
# self-contained .exe files: no MinGW runtime DLLs needed next to them
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
