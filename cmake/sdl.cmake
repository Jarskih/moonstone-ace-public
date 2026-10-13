# ROADMAP 11.4: the PC build, -DMS_PLATFORM=SDL (included by CMakeLists.txt; the Amiga build never reads this file).
#
#   cmake -S . -B build-sdl -G Ninja -DMS_PLATFORM=SDL -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/pc-i686-clang.cmake
#   cmake --build build-sdl                     -> build-sdl/moonstone.exe (+ SDL3.dll beside it)
#   build-sdl/moonstone.exe --smoke [frames]    -> SDL dummy video driver, N frames at 50 Hz, exit 0
#
# What is linked: src/engine, src/game, the generated data (tools/gen_data.py --target pc32: the same objects, little-endian fill
# table, i686 COFF aliases), the host-clean src/rt files (src/rt/host_clean.txt), the platform in src/rt/sdl/ (include/rt/platform/*.hpp)
# and src/rt/sdl/stubs.cpp, which stands in for every src/rt file that is not host-clean yet (each stub names the 11.x task that
# replaces it).  Decision 11.2: the first PC build is 32-bit (68k addresses are pointers), SDL3 lib/x86.

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
	message(WARNING "MS_PLATFORM=SDL: the game keeps 68k addresses in 32-bit cells (ROADMAP 11.2), use the i686 toolchain file cmake/toolchains/pc-i686-clang.cmake")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# SDL3 (owner decision 2026-10-10, replaces SDL2): the VC development kit unpacked at tools/sdl3/SDL3-3.4.18 (git-ignored; docs/PC_PORT.md
# section 7.4), or -DMS_SDL3_DIR=...
set(MS_SDL3_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools/sdl3/SDL3-3.4.18" CACHE PATH "SDL3 VC development kit (include/SDL3, lib/x86, lib/x64)")
if(NOT EXISTS "${MS_SDL3_DIR}/include/SDL3/SDL.h")
	message(FATAL_ERROR "MS_PLATFORM=SDL: SDL3 development kit not found at ${MS_SDL3_DIR} (unpack SDL3-devel-3.4.18-VC.zip there or pass -DMS_SDL3_DIR=)")
endif()
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
	set(MS_SDL3_ARCH x86)
else()
	set(MS_SDL3_ARCH x64)
endif()

set(MS_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(MS_TOOLS ${CMAKE_CURRENT_SOURCE_DIR}/tools)
set(MS_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/gen)

# --- sources -------------------------------------------------------------------------------------------------------------------
file(GLOB_RECURSE MS_SOURCES CONFIGURE_DEPENDS ${MS_SRC}/engine/*.cpp ${MS_SRC}/game/*.cpp)
# the synth tables are original data the game fills at start-up (src/rt/origload.cpp); the tracked copy is never compiled
list(FILTER MS_SOURCES EXCLUDE REGEX "/src/engine/synth_data[.]cpp$")
option(MS_EXAMPLE_SCENE "Example scene of docs/GAME_FLOW.md" OFF)
if(NOT MS_EXAMPLE_SCENE)
	list(FILTER MS_SOURCES EXCLUDE REGEX "/src/game/scenes/example[.]cpp$")
endif()
file(STRINGS ${MS_SRC}/rt/host_clean.txt MS_HOST_CLEAN REGEX "^[^#]")
foreach(f IN LISTS MS_HOST_CLEAN)
	if(f STREQUAL "example_scene.cpp" AND NOT MS_EXAMPLE_SCENE)
		continue()
	endif()
	list(APPEND MS_SOURCES ${MS_SRC}/rt/${f})
endforeach()
file(GLOB MS_SDL_SOURCES CONFIGURE_DEPENDS ${MS_SRC}/rt/sdl/*.cpp)
list(APPEND MS_SOURCES ${MS_SDL_SOURCES})

# --- generated data (same recipe as the Amiga build, other target) ----------------------------------------------------------------
file(GLOB MS_OWNED_DEPS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/asm/patches/*.json)
file(GLOB_RECURSE MS_NAME_DEPS CONFIGURE_DEPENDS ${MS_SRC}/*.cpp ${MS_SRC}/*.hpp ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
	${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp)
list(FILTER MS_NAME_DEPS EXCLUDE REGEX "/src/lifted/|/include/ms/gen/|/src/rt/gen/")
list(APPEND MS_OWNED_DEPS ${MS_NAME_DEPS} ${MS_TOOLS}/cell_names.yaml ${MS_TOOLS}/cellnames.py)
file(GLOB MS_FACTS CONFIGURE_DEPENDS ${MS_TOOLS}/facts/*.json)
add_custom_command(
	OUTPUT ${MS_GEN_DIR}/owned_data.cpp ${MS_GEN_DIR}/owned_data.hpp
	COMMAND ${Python3_EXECUTABLE} ${MS_TOOLS}/gen_data.py --target pc32 --out-dir ${MS_GEN_DIR}
	DEPENDS ${MS_TOOLS}/gen_data.py ${MS_TOOLS}/data_types.yaml ${MS_TOOLS}/tables.yaml ${MS_TOOLS}/data_units.yaml ${MS_OWNED_DEPS} ${MS_FACTS}
		${MS_TOOLS}/origfacts.py ${MS_TOOLS}/origin.py ${MS_TOOLS}/hunkfile.py
	COMMENT "gen_data --target pc32 (owned data, little-endian fill table, i686 COFF aliases)"
	VERBATIM
)
file(GLOB MS_MODSCHEMA_YAML CONFIGURE_DEPENDS ${MS_TOOLS}/mod_schema/*.yaml)
add_custom_command(
	OUTPUT ${MS_GEN_DIR}/mod_defaults.cpp ${MS_GEN_DIR}/mod_schema.cpp ${MS_GEN_DIR}/mod_names.cpp ${MS_GEN_DIR}/defaults/items.ini ${MS_GEN_DIR}/defaults/rules.ini
	COMMAND ${Python3_EXECUTABLE} ${MS_TOOLS}/gen_moddata.py --out-dir ${MS_GEN_DIR}
	DEPENDS ${MS_TOOLS}/gen_moddata.py ${MS_MODSCHEMA_YAML}
	COMMENT "gen_moddata (mod schema -> kDefaults, field tables)"
	VERBATIM
)
list(APPEND MS_SOURCES ${MS_GEN_DIR}/owned_data.cpp ${MS_GEN_DIR}/mod_defaults.cpp ${MS_GEN_DIR}/mod_schema.cpp ${MS_GEN_DIR}/mod_names.cpp)

# --- the executable ------------------------------------------------------------------------------------------------------------------
add_executable(moonstone ${MS_SOURCES})
target_include_directories(moonstone PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/include/host    # the ace/types.h stand-in (ROADMAP 11.1e) must win over any real ACE
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${MS_SRC}
	${MS_GEN_DIR})
target_include_directories(moonstone SYSTEM PRIVATE ${MS_SDL3_DIR}/include)
target_compile_definitions(moonstone PRIVATE
	MS_PLATFORM_SDL=1 MS_GAME_BUILD=1 MS_ENHANCED=0 MS_SYNTH_ASM=0 MS_COOP=1 MS_DATA_COMPILED=0 MS_DATA_DUMP=0
	SDL_MAIN_HANDLED _CRT_SECURE_NO_WARNINGS)
target_compile_options(moonstone PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions;-fno-rtti>)
# the shared rt files carry GCC attributes (externally_visible, optimize) that clang ignores; the Amiga build needs them
target_compile_options(moonstone PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wno-unknown-attributes>)
target_link_directories(moonstone PRIVATE ${MS_SDL3_DIR}/lib/${MS_SDL3_ARCH})
target_link_libraries(moonstone PRIVATE SDL3)
add_custom_command(TARGET moonstone POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MS_SDL3_DIR}/lib/${MS_SDL3_ARCH}/SDL3.dll $<TARGET_FILE_DIR:moonstone>
	COMMENT "SDL3.dll next to moonstone.exe")

# link.exe needs rc.exe to embed the manifest the Windows-Clang platform file asks for; the console program needs none
foreach(lang C CXX)
	string(REPLACE "/MANIFEST:EMBED" "/MANIFEST:NO" CMAKE_${lang}_LINK_EXECUTABLE "${CMAKE_${lang}_LINK_EXECUTABLE}")
endforeach()

# the new platform code is held to -Wall -Wextra (the inherited game code is not, yet)
set_source_files_properties(${MS_SDL_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
