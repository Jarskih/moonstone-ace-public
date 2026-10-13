# ROADMAP 11.2 / 11.4: the first PC build is 32-bit (68k addresses are held in 32-bit cells): clang for i686-pc-windows-msvc
# (MSVC runtime and SDK, found by clang itself).  Use with -DMS_PLATFORM=SDL; the x64 flavour is the later base-relative form (9.3d).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR X86)
set(MS_CLANG_BIN "C:/Program Files/LLVM/bin" CACHE PATH "directory of clang / clang++")
set(CMAKE_C_COMPILER   "${MS_CLANG_BIN}/clang.exe")
set(CMAKE_CXX_COMPILER "${MS_CLANG_BIN}/clang++.exe")
set(CMAKE_C_COMPILER_TARGET   i686-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET i686-pc-windows-msvc)
set(CMAKE_C_FLAGS_INIT   "--target=i686-pc-windows-msvc")
set(CMAKE_CXX_FLAGS_INIT "--target=i686-pc-windows-msvc")
# MSVC link.exe: the lld-link of clang 11 cannot resolve the newer CRT (___guard_eh_cont_count in msvcrtd.lib)
set(CMAKE_EXE_LINKER_FLAGS_INIT "--target=i686-pc-windows-msvc")
set(CMAKE_LINKER_TYPE MSVC)   # CMake 3.29+: -fuse-ld=link
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
# link.exe wants rc.exe for /MANIFEST:EMBED (not on PATH outside a VS prompt) and the compiler check would link a program on the way:
# the compiler is known to work (docs/PC_PORT.md 7.4), and cmake/sdl.cmake turns the embedded manifest off.
set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_CXX_COMPILER_WORKS 1)
set(CMAKE_SIZEOF_VOID_P 4)   # not detected: the compiler check above is skipped
