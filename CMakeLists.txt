cmake_minimum_required(VERSION 3.16)
project(nes-emu-cpp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    # RelWithDebInfo (-O2 -g) by default: the pixel/tile pipeline is hot
    # enough (per-frame per-tile Picture copies, PPU memory scans) that an
    # unoptimized Debug build is misleadingly slow for judging real emulator
    # speed, while still keeping debug symbols for gdb. Override explicitly
    # (-DCMAKE_BUILD_TYPE=Debug) when stepping through with no optimization.
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# Opt-in sanitizer support, added after this
# project's own history of exactly the two bug classes these catch: real
# out-of-bounds crashes (PPUPCADDR growing past NES_PPU_Memory::Memory's
# bounds and indexing a vector via operator[] - see
# NES_PPU/Memory/NES_PPU_Register.cpp's FIXED notes - is precisely what
# AddressSanitizer is built to catch immediately, with a full stack trace,
# instead of days of printf tracing to a segfault's root cause) and real
# data races (the CPU thread and UI thread share mutable state - e.g.
# Interrupt::nmi/irq/POWER, NES_Memory::Memory - with no synchronization
# beyond a handful of plain bools, exactly what ThreadSanitizer is built to
# catch). Usage: `cmake -DNES_SANITIZER=address ..` (or `thread`, `undefined`,
# or a comma-separated combination ASan/UBSan support together, though ASan
# and TSan can never be combined in the same build - that's a Clang/GCC
# limitation, not one imposed here). Off by default (empty string) since
# both sanitizers add real runtime overhead, undesirable for normal use.
set(NES_SANITIZER "" CACHE STRING "Comma-separated sanitizer(s) to build with, e.g. address, thread, undefined, or address,undefined. Empty = none.")
if(NES_SANITIZER)
    add_compile_options(-fsanitize=${NES_SANITIZER} -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=${NES_SANITIZER})
endif()

find_package(OpenCV REQUIRED)
find_package(SDL2 REQUIRED)

# Flat include list: the .cpp/.h files #include each other by bare filename
# (occasionally via an explicit "../Folder/..." path) rather than through a
# nested namespace of paths, so every source folder needs to be on the
# include path directly.
set(NES_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/NES.Memory
    ${CMAKE_SOURCE_DIR}/CPU/CPU
    ${CMAKE_SOURCE_DIR}/CPU/Exception
    ${CMAKE_SOURCE_DIR}/INES
    ${CMAKE_SOURCE_DIR}/NES.Console
    ${CMAKE_SOURCE_DIR}/NES.Controller/Controller
    ${CMAKE_SOURCE_DIR}/NES_PPU/Memory
    ${CMAKE_SOURCE_DIR}/NES_PPU/OAM
    ${CMAKE_SOURCE_DIR}/NES_PPU/Palette
    ${CMAKE_SOURCE_DIR}/NES_PPU/NES_PPU_Folder
    ${CMAKE_SOURCE_DIR}/ClassLibrary1
    ${CMAKE_SOURCE_DIR}/NES_APU
    ${CMAKE_SOURCE_DIR}/NES_Audio
    ${CMAKE_SOURCE_DIR}/Mappers
    ${CMAKE_SOURCE_DIR}/Profiler
    ${CMAKE_SOURCE_DIR}/NES
)

file(GLOB_RECURSE NES_CORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/NES.Memory/*.cpp
    ${CMAKE_SOURCE_DIR}/CPU/CPU/*.cpp
    ${CMAKE_SOURCE_DIR}/INES/*.cpp
    ${CMAKE_SOURCE_DIR}/NES.Console/*.cpp
    ${CMAKE_SOURCE_DIR}/NES.Controller/Controller/*.cpp
    ${CMAKE_SOURCE_DIR}/NES_PPU/Memory/*.cpp
    ${CMAKE_SOURCE_DIR}/NES_PPU/OAM/*.cpp
    ${CMAKE_SOURCE_DIR}/NES_PPU/Palette/*.cpp
    ${CMAKE_SOURCE_DIR}/NES_PPU/NES_PPU_Folder/*.cpp
    ${CMAKE_SOURCE_DIR}/ClassLibrary1/*.cpp
    ${CMAKE_SOURCE_DIR}/NES_APU/*.cpp
    ${CMAKE_SOURCE_DIR}/Mappers/*.cpp
)

add_executable(nes-emu ${NES_CORE_SOURCES}
    ${CMAKE_SOURCE_DIR}/NES/main.cpp
    ${CMAKE_SOURCE_DIR}/NES/KeyboardInputSource.cpp
    ${CMAKE_SOURCE_DIR}/NES/GamepadInputSource.cpp
    ${CMAKE_SOURCE_DIR}/NES/HdLayer.cpp
    ${CMAKE_SOURCE_DIR}/NES_Audio/NES_Audio.cpp
)

target_compile_definitions(nes-emu PRIVATE NES_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(nes-emu PRIVATE ${NES_INCLUDE_DIRS} ${OpenCV_INCLUDE_DIRS} ${SDL2_INCLUDE_DIRS})
target_link_libraries(nes-emu PRIVATE ${OpenCV_LIBS} ${SDL2_LIBRARIES} pthread)

# NES_PPU_Palette's default bitmap path ("./Palletes/2C03and2C05.bmp") and
# main.cpp's default ROM path are both relative to the working directory,
# so copy the palette asset next to the built binary; the default path then
# resolves when run from the build dir.
add_custom_command(TARGET nes-emu POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/NES_PPU/Palette/Palletes
        $<TARGET_FILE_DIR:nes-emu>/Palletes
)

enable_testing()
if(EXISTS ${CMAKE_SOURCE_DIR}/tests/CMakeLists.txt)
    add_subdirectory(tests)
endif()

# Headless speed test / benchmark (no window, no throttle) - see tools/nes_bench.cpp.
add_executable(nes-bench ${NES_CORE_SOURCES} ${CMAKE_SOURCE_DIR}/tools/nes_bench.cpp ${CMAKE_SOURCE_DIR}/NES/HdLayer.cpp)
target_include_directories(nes-bench PRIVATE ${NES_INCLUDE_DIRS} ${OpenCV_INCLUDE_DIRS})
target_link_libraries(nes-bench PRIVATE ${OpenCV_LIBS} pthread)
add_custom_command(TARGET nes-bench POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/NES_PPU/Palette/Palletes
        $<TARGET_FILE_DIR:nes-bench>/Palletes
)
