# Opt-in compiler cache (dev-loop speed): -DMS_CCACHE=ON routes every C/C++ compile through ccache (or sccache) when one is found.
# Not installed on the owner's machine by default; configuring with ON and no ccache on PATH only warns and builds as before.
# Pick another launcher with -DMS_CCACHE_PROGRAM=<path>.  Cache size / location: ccache's own CCACHE_DIR / CCACHE_MAXSIZE.
# Must be included before the first target is created (the launcher variables initialise the targets' properties).
option(MS_CCACHE "Compile through ccache when available (opt-in; see BUILD.md)" OFF)
set(MS_CCACHE_PROGRAM "" CACHE FILEPATH "ccache/sccache executable (default: found on PATH)")
if(MS_CCACHE)
	if(MS_CCACHE_PROGRAM)
		set(_ms_ccache "${MS_CCACHE_PROGRAM}")
	else()
		find_program(_ms_ccache NAMES ccache sccache)
	endif()
	if(_ms_ccache)
		message(STATUS "MS_CCACHE: compiling through ${_ms_ccache}")
		set(CMAKE_C_COMPILER_LAUNCHER "${_ms_ccache}")
		set(CMAKE_CXX_COMPILER_LAUNCHER "${_ms_ccache}")
	else()
		message(WARNING "MS_CCACHE=ON but no ccache/sccache found on PATH (install one, or pass -DMS_CCACHE_PROGRAM=...); building without a cache")
	endif()
endif()
