# host/xbox/CMakeLists.txt -- the Xbox program (docs/xbox.md): a UWP app
# for an Xbox One or Series console in Developer Mode.
#
# The game is the 64-bit route's generated C made with bounds checks, as
# the Switch's is (docs/consoles.md): a host build of host/ configured with
# EIDOS_HOST_BOUNDS_CHECKS=ON gives game_*.c, game.h and gl_imports.c.  The
# host is SDL3's, answered by the SDL3-on-SDL2 layer (host/switch/sw2.h),
# because SDL2 is the last SDL with a UWP port; the SDL2 half is its own
# library (eidos_sdl2.dll), so the program never sees SDL2's names.  OpenGL
# ES 3.0 comes from ANGLE (libEGL.dll, libGLESv2.dll), on Direct3D 11.
#
# Configure from a Visual Studio x64 prompt set up for UWP
# (vcvarsall.bat x64 uwp), with Ninja:
#
#   cmake -S host/xbox -B build-xbox -G Ninja -DCMAKE_BUILD_TYPE=Release
#         -DEIDOS_GAME_DIR=<host build with the generated C>
#         -DWABT_ROOT=<wabt source> -DSDL3_INCLUDE=<SDL3 include dir>
#         -DSDL2_PREFIX=<SDL2 built for UWP and installed>
#
# tools/xbox_package.py then makes the two packages from the program.

cmake_minimum_required(VERSION 3.20)
# a UWP program's system library is WindowsApp.lib, not kernel32.lib and the
# rest (the store CRT needs RoInitialize from it).  CMake's compiler check
# links a desktop program with the store CRT, which fails, so it is skipped.
set(CMAKE_C_COMPILER_WORKS 1)
project(eidos_xbox C)
set(CMAKE_C_STANDARD_LIBRARIES "WindowsApp.lib")

set(EIDOS_GAME_DIR "" CACHE PATH "a host build holding game_*.c, game.h and gl_imports.c")
set(WABT_ROOT "" CACHE PATH "wabt's source tree (its wasm2c/ runtime)")
set(SDL3_INCLUDE "" CACHE PATH "SDL3's include dir (the host's types)")
set(SDL2_PREFIX "" CACHE PATH "SDL2 for UWP: include/SDL2, lib/SDL2.lib")
set(EIDOS_HOST_MEMORY_MB 512 CACHE STRING "the module's memory (MiB)")

if(NOT MSVC)
    message(FATAL_ERROR "the Xbox build is MSVC's (a UWP app)")
endif()
foreach(_f ${EIDOS_GAME_DIR}/game.h ${EIDOS_GAME_DIR}/gl_imports.c
           ${WABT_ROOT}/wasm2c/wasm-rt-impl.c ${SDL3_INCLUDE}/SDL3/SDL.h
           ${SDL2_PREFIX}/include/SDL2/SDL.h)
    if(NOT EXISTS ${_f})
        message(FATAL_ERROR "nothing at ${_f}")
    endif()
endforeach()
find_library(EIDOS_SDL2_LIB NAMES SDL2 PATHS ${SDL2_PREFIX}/lib NO_DEFAULT_PATH REQUIRED)

set(_root ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(_here ${CMAKE_CURRENT_SOURCE_DIR})
set(_sw ${_root}/host/switch)

# A UWP app: the app family's headers, the app container, WindowsApp.lib
# for the system's functions (the UWP prompt's LIB has the store CRT).
add_compile_definitions(WINAPI_FAMILY=WINAPI_FAMILY_APP _CRT_SECURE_NO_WARNINGS
    _CRT_NONSTDC_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS EIDOS_XBOX=1)
add_compile_options(/std:c17 /utf-8 /MD /W1 /wd4005)
add_link_options(/APPCONTAINER /NODEFAULTLIB:kernel32.lib /NODEFAULTLIB:user32.lib
    /NODEFAULTLIB:ole32.lib /DYNAMICBASE /NXCOMPAT /DEBUG /OPT:REF /OPT:ICF)

# ------------------------------------------------------------ SDL2's half
add_library(eidos_sdl2 SHARED ${_sw}/sdl2_side.c)
target_include_directories(eidos_sdl2 PRIVATE ${_sw} ${SDL2_PREFIX}/include/SDL2)
target_link_libraries(eidos_sdl2 PRIVATE ${EIDOS_SDL2_LIB})
set_target_properties(eidos_sdl2 PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

# ------------------------------------------------------------ the program
file(GLOB _game ${EIDOS_GAME_DIR}/game_*.c)
list(LENGTH _game _n)
if(_n EQUAL 0)
    message(FATAL_ERROR "no game_*.c in ${EIDOS_GAME_DIR}")
endif()
set(_rt ${WABT_ROOT}/wasm2c/wasm-rt-impl.c ${WABT_ROOT}/wasm2c/wasm-rt-mem-impl.c
        ${WABT_ROOT}/wasm2c/wasm-rt-exceptions-impl.c)
set(_host ${EIDOS_GAME_DIR}/gl_imports.c
          ${_root}/host/main.c ${_root}/host/fiber.c ${_root}/host/par.c
          ${_root}/host/net.c ${_root}/host/wasi.c ${_root}/host/sdl.c
          ${_root}/host/sdl_gpu.c
          ${_sw}/sdl3_on_sdl2.c ${_sw}/sdl3_gpu_stubs.c
          ${_here}/xbox_host.c)
# the host and the runtime first (a target of their own, so a build can
# check them in minutes before the generated C's hour)
add_library(eidos_host OBJECT ${_rt} ${_host})
add_executable(eidos WIN32 ${_game} $<TARGET_OBJECTS:eidos_host>)
foreach(_t eidos_host eidos)
    target_include_directories(${_t} PRIVATE
        ${EIDOS_GAME_DIR} ${_root}/host ${_root}/install ${_root}/sdk
        ${WABT_ROOT}/wasm2c ${SDL3_INCLUDE} ${_sw})
    target_compile_definitions(${_t} PRIVATE
        WASM_RT_NONCONFORMING_UNCHECKED_STACK_EXHAUSTION=1
        WASM_RT_SKIP_SIGNAL_RECOVERY=1
        WASM_RT_TRAP_HANDLER=host_trap
        WASM_RT_USE_MMAP=0
        WASM_RT_MEMCHECK_BOUNDS_CHECK=1
        HOST_MEMORY_BUDGET_MB=${EIDOS_HOST_MEMORY_MB}
        HOST_GLES=1
        SDL_MAIN_HANDLED=1
        _WIN32_WINNT=0x0A00)
    target_compile_options(${_t} PRIVATE $<$<CONFIG:Release>:/O2 /Ob1>)
endforeach()
# the host's own files take the compatibility header first (POSIX names
# MSVC spells otherwise); the generated C and the runtime do not need it
set_source_files_properties(${_host} PROPERTIES
    COMPILE_OPTIONS "/FI${_here}/xbox_compat.h")
# the generated files are large
set_source_files_properties(${_game} PROPERTIES COMPILE_OPTIONS "/bigobj")
target_link_libraries(eidos PRIVATE eidos_sdl2 ws2_32.lib bcrypt.lib)
# a stack of 8 MiB for the program's thread, as the desktop's
target_link_options(eidos PRIVATE /STACK:8388608)
