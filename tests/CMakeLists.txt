# Test harnesses for the ported core - separate from the main nes-emu
# executable (which pulls in NES/main.cpp's UI shell). Reuses the same core
# sources and include layout as the top-level CMakeLists.txt.
find_package(OpenCV REQUIRED)

set(NES_TEST_INCLUDE_DIRS
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
    ${CMAKE_SOURCE_DIR}/Mappers
    ${CMAKE_SOURCE_DIR}/Profiler
    ${CMAKE_SOURCE_DIR}/NES
)

file(GLOB_RECURSE NES_TEST_CORE_SOURCES CONFIGURE_DEPENDS
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

add_executable(nestest_check ${NES_TEST_CORE_SOURCES} nestest/nestest_check.cpp)
target_include_directories(nestest_check PRIVATE ${NES_TEST_INCLUDE_DIRS} ${OpenCV_INCLUDE_DIRS})
target_link_libraries(nestest_check PRIVATE ${OpenCV_LIBS} pthread)

# NES_PPU_Palette's default path ("./Palletes/2C03and2C05.bmp") is relative
# to the working directory, which for a ctest-run test is this binary's own
# directory (build/tests), not the top build dir - the main nes-emu target
# already copies this asset next to itself for the same reason (see the
# top-level CMakeLists.txt); nestest_check needs its own copy too.
add_custom_command(TARGET nestest_check POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/NES_PPU/Palette/Palletes
        $<TARGET_FILE_DIR:nestest_check>/Palletes
)

add_test(NAME nestest
    COMMAND nestest_check
        ${CMAKE_CURRENT_SOURCE_DIR}/nestest/nestest.nes
        ${CMAKE_CURRENT_SOURCE_DIR}/nestest/nestest.log
)

# Mapper-correctness harness (see tests/mappers/mapper_check.cpp) - synthetic
# PRG/CHR data, no real .nes file needed, so no POST_BUILD asset copy beyond
# the Palletes/ folder every core-linking test binary needs (NES_PPU_Palette
# still loads its default palette bitmap from a relative path at INIT()).
add_executable(mapper_check ${NES_TEST_CORE_SOURCES} mappers/mapper_check.cpp)
target_include_directories(mapper_check PRIVATE ${NES_TEST_INCLUDE_DIRS} ${OpenCV_INCLUDE_DIRS})
target_link_libraries(mapper_check PRIVATE ${OpenCV_LIBS} pthread)

add_custom_command(TARGET mapper_check POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/NES_PPU/Palette/Palletes
        $<TARGET_FILE_DIR:mapper_check>/Palletes
)

add_test(NAME mapper_check COMMAND mapper_check)

# CPU/interrupt/PPU-register harness (see tests/cpu/cpu_check.cpp) - direct
# synthetic-program tests, no real .nes file needed.
add_executable(cpu_check ${NES_TEST_CORE_SOURCES} cpu/cpu_check.cpp)
target_include_directories(cpu_check PRIVATE ${NES_TEST_INCLUDE_DIRS} ${OpenCV_INCLUDE_DIRS})
target_link_libraries(cpu_check PRIVATE ${OpenCV_LIBS} pthread)

add_custom_command(TARGET cpu_check POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/NES_PPU/Palette/Palletes
        $<TARGET_FILE_DIR:cpu_check>/Palletes
)

add_test(NAME cpu_check COMMAND cpu_check)

# This whole codebase leans on permanent static/global singletons
# (NES_Memory::Memory, NES_PPU_Memory::*, ...) that are never explicitly
# torn down before process exit - fine for a program that only ever exits
# by the process ending, but LeakSanitizer (bundled with AddressSanitizer)
# flags every one of those as "leaked" by default, since from its view
# nothing ever freed them. That's real noise, not a real bug - see
# CMakeLists.txt's own NES_SANITIZER comment for the actual bugs ASan/TSan
# are here to catch (out-of-bounds writes, data races) - so leak detection
# is switched off for exactly the sanitizer build where it would otherwise
# drown those out on every single run.
if(NES_SANITIZER MATCHES "address")
    set_tests_properties(nestest mapper_check cpu_check PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
endif()
