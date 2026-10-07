cmake_minimum_required(VERSION 3.14.0)
project(moonstone LANGUAGES C)

# The shared toolchain file selects gcc for CXX. Select its sibling g++
# before enabling CXX (same recipe as ../moonshard). ACE stays compiled as C.
get_filename_component(MS_COMPILER_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
if(CMAKE_HOST_WIN32)
	set(MS_COMPILER_SUFFIX ".exe")
endif()
set(CMAKE_CXX_COMPILER "${MS_COMPILER_DIR}/${TOOLCHAIN_PREFIX}-g++${MS_COMPILER_SUFFIX}")
enable_language(CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Debug: no LTO / whole-program for C, C++ and the (C-driver) link, otherwise
# -fwhole-program at link hides ACE symbols from the non-LTO C++ objects.
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -fno-lto -fno-whole-program")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -fno-lto -fno-whole-program")

if(NOT AMIGA)
	message(SEND_ERROR "This project only compiles for Amiga - point CMake at m68k-bartman.cmake toolchain file")
endif()

# ACE: the submodule of the public repository (./ace), else the sibling checkout of the D:/Amiga workspace (../ace).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ace/CMakeLists.txt")
	set(MS_ACE_DEFAULT ${CMAKE_CURRENT_SOURCE_DIR}/ace)
else()
	set(MS_ACE_DEFAULT ${CMAKE_CURRENT_SOURCE_DIR}/../ace)
endif()
set(ACE_PATH ${MS_ACE_DEFAULT} CACHE PATH "Path to ACE checkout")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/bartman_gcc_support/CMakeLists.txt")
	set(CPM_bartman_gcc_support_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tools/bartman_gcc_support")
endif()
add_subdirectory(${ACE_PATH} ace)

file(GLOB_RECURSE MS_SOURCES CONFIGURE_DEPENDS
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
)
# Lifted routines (src/lifted, M3): the 68k->C++ lifts used by the differential tests (tools/diffharness, host clang++). Turn
# MS_LINK_LIFTED ON to compile them with the cross toolchain as a compile check only: no asm is linked any more (ROADMAP 7.1r), so
# nothing can call them and the swap-into-asm thunks of ROADMAP 3.2 (src/rt/thunks.cpp, src/rt/gen/hunk_tab.cpp, include/ms/linked.hpp,
# tools/thunks.py, MS_SWAP_LIST) are retired from the build; the tools and their host tests stay.
option(MS_LINK_LIFTED "Compile src/lifted/** with the cross toolchain (compile check; the host tests do not need it)" OFF)
if(NOT MS_LINK_LIFTED)
	list(FILTER MS_SOURCES EXCLUDE REGEX "/src/lifted/")
endif()
# regs.hpp flag self-tests are host-only (tools/diffharness/run_host.py)
list(FILTER MS_SOURCES EXCLUDE REGEX "/src/lifted/nb/selftest_[^/]*$")
# stale lift output whose function name is not a C++ identifier (lab_LAB_0426+2); never compiled
list(FILTER MS_SOURCES EXCLUDE REGEX "/src/lifted/[^/]*/[^/]*[+][^/]*$")
# the asm thunk runtime (retired, see above)
list(FILTER MS_SOURCES EXCLUDE REGEX "/src/rt/(thunks[.]cpp|gen/hunk_tab[.]cpp)$")
# ROADMAP 10.2a: the synth tables are original data; the game fills them at start-up (src/rt/origload.cpp). The old tracked copy
# src/engine/synth_data.cpp is never compiled; MS_DATA_COMPILED generates it into the build directory instead.
list(FILTER MS_SOURCES EXCLUDE REGEX "/src/engine/synth_data[.]cpp$")

# ---------------------------------------------------------------------------
# NO GAME ASM IS LINKED (ROADMAP 7.1r). Both overlays (program, mog) are C++ end to end: the code is src/{engine,game,rt}, the
# data is generated into build/gen/owned_data.{hpp,cpp} by tools/gen_data.py (the DATA/BSS hunks, the data cells that lay in code
# hunks, the overlay-entry reset table), and the entry is rtGameRun (src/rt/game.cpp). asm/*.s and asm/patches/*.json are kept as
# the reference listing (tools/resource.py --verify proves them load-equivalent to the original) and for the one optional piece:
#   MS_SYNTH_ASM  A/B listening: link mog's ORIGINAL synth (S_44, asm/synth.s, assembled by vasm) instead of the C++ synth.
# ---------------------------------------------------------------------------
# Moonstone 2 co-op (ROADMAP 8.2, docs/MOONSTONE2.md): ON offers "Players ... Coop 2" in the title menu; the game still starts
# in classic mode (game/party.hpp g_party inactive) and classic stays bit-identical. OFF removes the menu step (g_party never on).
option(MS_COOP "Moonstone 2 co-op mode selectable in the title menu (runtime default: classic)" ON)
option(MS_ENHANCED "6-plane 64-colour display for the redrawn art (ROADMAP 4.8a, include/rt/enhanced.hpp)" OFF)
# Rule mods are data, not build options: PROGDIR:mods/rules.ini (docs/MODDING.md; `[waves] scaling = none` replaced MS_MOD_NO_SCALING).
# The "How to add a scene" example of docs/GAME_FLOW.md (ROADMAP 9.2a): a placeholder scene after the map's status sheet that
# shows one picture and waits for fire. OFF: its two files are not even compiled (the default game is unchanged).
option(MS_EXAMPLE_SCENE "Example scene of docs/GAME_FLOW.md (status sheet -> picture -> map)" OFF)
if(NOT MS_EXAMPLE_SCENE)
	list(FILTER MS_SOURCES EXCLUDE REGEX "/src/(game/scenes/example|rt/example_scene)[.]cpp$")
endif()
option(MS_SYNTH_ASM "Run mog's original asm synth (asm/synth.s) instead of the C++ port (A/B listening, ROADMAP 7.1g)" OFF)
set(MS_VASM ${CMAKE_CURRENT_SOURCE_DIR}/tools/toolchain/vasmm68k_mot.exe CACHE FILEPATH "vasm m68k (Motorola syntax), only used for MS_SYNTH_ASM")

# ROADMAP 7.1n1: the DATA/BSS hunks C++ owns (asm/patches/<bin>.data*.json "extern_data"), the code-hunk cells (7.1r
# "extern_cells") and the overlay-entry reset tables, generated by tools/gen_data.py into <build dir>/gen (the contents are
# derived from the original game). The label aliases (`.set prg_LAB_0362, g_ioFlags+1`) are the C++ names of the cells; the
# TU is compiled -fno-lto because an alias needs the object in the same assembler file (a linker-script assignment would give
# absolute symbols, which elf2hunk cannot relocate).
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(MS_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/gen)   # per build directory: MS_SYNTH_ASM changes the generated pointer cells
# every patch table is an input: gen_data applies the line patches that lie inside an owned hunk (7.1n2) and reads the cell lists
file(GLOB MS_OWNED_DEPS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/asm/patches/*.json)
# ROADMAP 7.1s: tools/cell_names.yaml gives the cells their C++ names, and gen_data generates a `prg_LAB_xxxx` label alias only for what the
# sources still spell, so the sources are inputs too (an unchanged output leaves the dependents alone)
file(GLOB_RECURSE MS_NAME_DEPS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
	${CMAKE_CURRENT_SOURCE_DIR}/include/*.h ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)
list(FILTER MS_NAME_DEPS EXCLUDE REGEX "/src/lifted/|/include/ms/gen/|/src/rt/gen/|/[.]vs/")
list(APPEND MS_OWNED_DEPS ${MS_NAME_DEPS} ${CMAKE_CURRENT_SOURCE_DIR}/tools/cell_names.yaml ${CMAKE_CURRENT_SOURCE_DIR}/tools/cellnames.py)
set(MS_GEN_ARGS)
if(MS_SYNTH_ASM)
	list(APPEND MS_GEN_ARGS --asm-labels)
	list(APPEND MS_OWNED_DEPS ${CMAKE_CURRENT_SOURCE_DIR}/asm/synth.s)   # its label names (developer checkout only, ROADMAP 10.2)
endif()
# ROADMAP 10.2a/c: by default the build reads no original file: gen_data writes the data layout from our facts (tools/facts/<bin>.json)
# with every original byte zero plus the start-up fill table, and the game reads program and mog from the player's disks.
#   MS_DATA_COMPILED  developer check: compile the original data in (gen_data --full, reads ${MS_DISKS_DIR}/A/{program,mog}).
#   MS_DATA_DUMP      write PROGDIR:datadump.bin after the start-up (tools/datadump.py compares a filled and a compiled build).
option(MS_DATA_COMPILED "Compile the original data in (needs the extracted disks; the default fills it at start-up)" OFF)
option(MS_DATA_DUMP "Dump the owned data to PROGDIR:datadump.bin at start-up (tools/datadump.py)" OFF)
set(MS_DISKS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/build/disks CACHE PATH "Extracted original disks (tools/adfx.py output: A/ B/ C/)")
set(MS_ORIG_DEPS)
if(MS_DATA_COMPILED)
	list(APPEND MS_GEN_ARGS --full --orig-dir ${MS_DISKS_DIR}/A)
	set(MS_ORIG_DEPS ${MS_DISKS_DIR}/A/program ${MS_DISKS_DIR}/A/mog)
endif()
file(GLOB MS_FACTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/facts/*.json)
add_custom_command(
	OUTPUT ${MS_GEN_DIR}/owned_data.cpp ${MS_GEN_DIR}/owned_data.hpp
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_data.py --out-dir ${MS_GEN_DIR} ${MS_GEN_ARGS}
	DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_data.py ${CMAKE_CURRENT_SOURCE_DIR}/tools/data_types.yaml ${CMAKE_CURRENT_SOURCE_DIR}/tools/tables.yaml ${MS_OWNED_DEPS}
		${MS_FACTS} ${CMAKE_CURRENT_SOURCE_DIR}/tools/origfacts.py ${CMAKE_CURRENT_SOURCE_DIR}/tools/origin.py
		${CMAKE_CURRENT_SOURCE_DIR}/tools/hunkfile.py ${MS_ORIG_DEPS}
	COMMENT "gen_data (C++-owned DATA/BSS hunks, code-hunk cells, reset tables)"
	VERBATIM
)
list(APPEND MS_SOURCES ${MS_GEN_DIR}/owned_data.cpp)
if(MS_DATA_COMPILED)
	add_custom_command(
		OUTPUT ${MS_GEN_DIR}/synth_data.cpp
		COMMAND ${CMAKE_COMMAND} -E env MS_DISKS=${MS_DISKS_DIR} ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_synth_tables.py --out ${MS_GEN_DIR}/synth_data.cpp
		DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_synth_tables.py ${MS_ORIG_DEPS}
		COMMENT "gen_synth_tables (MS_DATA_COMPILED: the synth tables compiled in)"
		VERBATIM
	)
	list(APPEND MS_SOURCES ${MS_GEN_DIR}/synth_data.cpp)
endif()
set_source_files_properties(${MS_GEN_DIR}/owned_data.cpp PROPERTIES COMPILE_OPTIONS "-fno-lto;-fno-whole-program")

# ROADMAP 9.4b: the mod data schema (tools/mod_schema/*.yaml, docs/ARCHITECTURE.md 3.2) -> the compiled defaults (`const GameData
# kDefaults`), the parser's field tables and the reference .ini files (<build dir>/gen/defaults/, staged by hdinstall in 9.4c).
# docs/MOD_KEYS.md is written by hand-running `py tools/gen_moddata.py --doc docs/MOD_KEYS.md` (tests/test_gen_moddata.py checks it).
file(GLOB MS_MODSCHEMA_YAML CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/mod_schema/*.yaml)
add_custom_command(
	OUTPUT ${MS_GEN_DIR}/mod_defaults.cpp ${MS_GEN_DIR}/mod_schema.cpp ${MS_GEN_DIR}/mod_names.cpp ${MS_GEN_DIR}/defaults/items.ini ${MS_GEN_DIR}/defaults/rules.ini
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_moddata.py --out-dir ${MS_GEN_DIR}
	DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_moddata.py ${MS_MODSCHEMA_YAML}
	COMMENT "gen_moddata (mod schema -> kDefaults, field tables, defaults/*.ini)"
	VERBATIM
)
list(APPEND MS_SOURCES ${MS_GEN_DIR}/mod_defaults.cpp ${MS_GEN_DIR}/mod_schema.cpp ${MS_GEN_DIR}/mod_names.cpp)
# the reader runs once at boot: size, not speed (-O1 inlines the message formatting: modparse.o 9.3 KB -> 4.3 KB with -Os)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/src/engine/inifile.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/game/data/modparse.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/game/data/modcheck.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/game/data/modload.cpp ${CMAKE_CURRENT_SOURCE_DIR}/src/game/data/modtopics.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/src/rt/modload.cpp PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Release>:-Os>")

# MS_SYNTH_ASM: the one asm object that can still be linked.
set(MS_ASM_OBJECTS)
if(MS_SYNTH_ASM)
	set(MS_SYNTH_OBJ ${CMAKE_CURRENT_BINARY_DIR}/asm/synth.o)
	add_custom_command(
		OUTPUT ${MS_SYNTH_OBJ}
		COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/asm
		COMMAND ${MS_VASM} -Felf -m68020 -devpac -no-opt -quiet -o ${MS_SYNTH_OBJ} ${CMAKE_CURRENT_SOURCE_DIR}/asm/synth.s
		DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/asm/synth.s
		COMMENT "vasm synth (MS_SYNTH_ASM: mog's original synth hunk)"
		VERBATIM
	)
	list(APPEND MS_ASM_OBJECTS ${MS_SYNTH_OBJ})
endif()

if(ELF2HUNK)
	# Bartman's compiler produces ELF - convert to Amiga hunk exe afterwards
	set(MS_EXECUTABLE moonstone.elf)
	set(MS_OUTPUT_EXECUTABLE moonstone)
	add_executable(${MS_EXECUTABLE} ${MS_SOURCES} ${MS_ASM_OBJECTS})
	add_custom_command(
		TARGET ${MS_EXECUTABLE} POST_BUILD
		COMMAND ${ELF2HUNK} ${MS_EXECUTABLE} ${MS_OUTPUT_EXECUTABLE}
		COMMAND ${CMAKE_COMMAND} -E copy ${MS_OUTPUT_EXECUTABLE} moonstone.exe
	)
else()
	set(MS_EXECUTABLE moonstone)
	set(MS_OUTPUT_EXECUTABLE moonstone)
	add_executable(${MS_EXECUTABLE} ${MS_SOURCES} ${MS_ASM_OBJECTS})
endif()

target_include_directories(${MS_EXECUTABLE} PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(${MS_EXECUTABLE} ace)
target_compile_definitions(${MS_EXECUTABLE} PRIVATE MS_DATA_COMPILED=$<BOOL:${MS_DATA_COMPILED}> MS_DATA_DUMP=$<BOOL:${MS_DATA_DUMP}>)
target_compile_definitions(${MS_EXECUTABLE} PRIVATE MS_GAME_BUILD=1 MS_ENHANCED=$<BOOL:${MS_ENHANCED}> MS_SYNTH_ASM=$<BOOL:${MS_SYNTH_ASM}>)
target_compile_definitions(${MS_EXECUTABLE} PRIVATE MS_COOP=$<BOOL:${MS_COOP}>)
if(MS_EXAMPLE_SCENE)
	target_compile_definitions(${MS_EXECUTABLE} PRIVATE MS_EXAMPLE_SCENE=1)
endif()
# Headless boot test (docs/AUTOPLAY.md): PROGDIR:autoplay.txt injects keys/joystick, the serial log carries the
# AUTOPLAY lines; -include makes the game's own logWrite() calls reach the serial log too. Use a separate build dir.
option(MS_AUTOPLAY "Scripted input + serial log for headless boot tests (build-autoplay/)" OFF)
if(MS_AUTOPLAY)
	target_compile_definitions(${MS_EXECUTABLE} PRIVATE MS_AUTOPLAY=1)
	target_compile_options(${MS_EXECUTABLE} PRIVATE
		$<$<COMPILE_LANGUAGE:CXX>:-include;${CMAKE_CURRENT_SOURCE_DIR}/src/rt/serlog_logwrite.hpp>)
endif()
# Keep every section so the link check reports all undefined symbols and the map shows every section.
target_link_options(${MS_EXECUTABLE} PRIVATE -Wl,--no-gc-sections -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/moonstone.map)
# Release/LTO: -fipa-icf (in the toolchain's -O1 set) folded rtCelMirror_mog into rtCelMirror_prg as a 6-byte `bra.l` (68020 long
# branch) whose PC32 field elf2hunk then relocated as an absolute (+ hunk base): the first mirrored cel in a fight jumped into
# DATA (Line-F crash, ROADMAP 7.2 finding). Folding saves little here; keep every function distinct.
target_compile_options(${MS_EXECUTABLE} PRIVATE -fno-ipa-icf)
target_link_options(${MS_EXECUTABLE} PRIVATE -fno-ipa-icf)
target_include_directories(${MS_EXECUTABLE} PRIVATE ${MS_GEN_DIR})
# No C++ runtime in the Amiga startup: link with the C driver and forbid
# features needing unwinding/typeinfo/guard variables. No global ctors run.
set_target_properties(${MS_EXECUTABLE} PROPERTIES LINKER_LANGUAGE C)
target_compile_options(${MS_EXECUTABLE} PRIVATE
	$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions;-fno-rtti;-fno-threadsafe-statics>)
if(ELF2HUNK AND CMAKE_BUILD_TYPE STREQUAL "Release")
	# Bartman's debugger logging is not supported by ordinary WinUAE.
	target_link_options(${MS_EXECUTABLE} PRIVATE -Wl,--wrap=KPrintF)
endif()

# Bootable ADF: stage exe + startup-sequence (LF-only!), pack with exe2adf
set(ADF_DIR ${CMAKE_CURRENT_BINARY_DIR}/adf)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
# CMake text output on Windows is CRLF; AmigaOS needs LF, so write bytes via Python.
set(MS_STARTUP_PY "import sys; open(sys.argv[1], 'wb').write((sys.argv[2] + chr(10)).encode())")
add_custom_target(adf
	COMMAND ${CMAKE_COMMAND} -E make_directory ${ADF_DIR}/s
	COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/${MS_OUTPUT_EXECUTABLE} ${ADF_DIR}
	COMMAND ${Python3_EXECUTABLE} -c "${MS_STARTUP_PY}" ${ADF_DIR}/s/startup-sequence ${MS_OUTPUT_EXECUTABLE}
	COMMAND exe2adf -l moonstone -a ${CMAKE_CURRENT_BINARY_DIR}/moonstone.adf -d ${ADF_DIR}
	DEPENDS ${MS_EXECUTABLE}
	COMMENT "Packing moonstone.adf"
)

# HD install tree (ROADMAP 2.9): build dir hd/ = moonstone + s/startup-sequence + data/ (all three
# original disks merged; needs `py tools/adfx.py build/disks A.adf B.adf C.adf` first, so the
# disks dir is the Release build's ${MS_DISKS_DIR}). Nothing in hd/ is committed.
set(MS_DISKS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/build/disks CACHE PATH "Extracted original disks (tools/adfx.py output: A/ B/ C/)")
# ROADMAP 5.2a: optional redrawn art (tools/artconv.py import --all --planes 5 -> build/art/import/{A,B,C}/),
# merged into hd/art/ where rt/files serves it before the disk original. Checked at configure time.
set(MS_ART_COPY "")
# The plain 32-colour build serves no art overrides (owner decision 2026-10-06: the redrawn art is 6-plane and enhanced-only;
# a plain build would refuse nearly all of it and pick up the odd small cel). Only MS_ENHANCED copies build/art/import6.
if(MS_ENHANCED)
	set(MS_ART_SRC import6)  # 6-plane art + .pal sidecars (tools/artconv.py --planes 6)
else()
	set(MS_ART_SRC none)
endif()
foreach(MS_ART_DISK A B C)
	if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/build/art/${MS_ART_SRC}/${MS_ART_DISK})
		list(APPEND MS_ART_COPY COMMAND ${CMAKE_COMMAND} -E copy_directory
			${CMAKE_CURRENT_SOURCE_DIR}/build/art/${MS_ART_SRC}/${MS_ART_DISK} ${CMAKE_CURRENT_BINARY_DIR}/hd/art)
	endif()
endforeach()
add_custom_target(hdinstall
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/hdstage.py
		${CMAKE_CURRENT_BINARY_DIR}/${MS_OUTPUT_EXECUTABLE} ${MS_DISKS_DIR} ${CMAKE_CURRENT_BINARY_DIR}/hd ${MS_GEN_DIR}/defaults
	${MS_ART_COPY}
	DEPENDS ${MS_EXECUTABLE}
	COMMENT "Staging HD install tree ${CMAKE_CURRENT_BINARY_DIR}/hd"
)
