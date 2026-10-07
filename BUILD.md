# Building moonstone-ace

Cross-build of an ACE C++ program (Bartman gcc 15.1, 68020) to `.elf` ->
Amiga hunk `.exe` -> bootable `.adf`. Git Bash on Windows.

## Toolchain PATH (every shell)

```sh
export PATH="/d/Amiga/moonstone-ace/tools/toolchain/opt/bin:/d/Amiga/moonstone-ace/tools/toolchain:/d/Amiga/moonstone-ace/tools/toolchain/opt/m68k-amiga-elf/bin:/c/msys64/mingw64/bin:$PATH"
```

## Configure

```sh
P=D:/Amiga/moonstone-ace
TC="-DCMAKE_TOOLCHAIN_FILE=$P/tools/AmigaCMakeCrossToolchains/m68k-bartman.cmake -DTOOLCHAIN_PREFIX=m68k-amiga-elf -DTOOLCHAIN_PATH=$P/tools/toolchain/opt -DM68K_CPU=68020"

# Release  -> build/        (note: build/ also holds git-ignored extracted disk data)
cmake -S $P -B $P/build -G Ninja $TC -DCMAKE_BUILD_TYPE=Release
# Debug    -> build-debug/  (-g -O0, no LTO, KPrintF kept)
cmake -S $P -B $P/build-debug -G Ninja $TC -DCMAKE_BUILD_TYPE=Debug
```

ACE is shared at `../ace/`; its Bartman support library is in `tools/bartman_gcc_support/`,
so configuring with these local copies does not require a network fetch.
Dependencies can be copied from the original workspace with `py tools/vendor_local.py`.
Use a fresh build directory when switching toolchains; existing CMake caches retain
their compiler and dependency paths. `ACE_PATH` and `MS_VASM` remain overridable.

## Build

```sh
cmake --build $P/build                       # moonstone.elf + moonstone (hunk) + moonstone.exe
cmake --build $P/build --target adf          # build/moonstone.adf (bootable, LF startup-sequence)
cmake --build $P/build-debug && cmake --build $P/build-debug --target adf
```

## Run

```sh
"/c/Program Files/WinUAE/winuae64.exe" -config="D:\Amiga\moonstone-ace\moonstone-ace.uae"
```

A1200 / AGA / 68020 / KS3.1, boots `build/moonstone.adf`. ESC or joystick
fire exits. Screenshot automation: `uaeshot.ps1` in this project.

## Layout / notes

- Sources are globbed from `src/**/*.cpp` and `src/**/*.c`; headers in `include/` and `src/`.
- `src/rt/` holds runtime subsystems (system, display, text); `src/main.cpp` is the ACE entry.
- vasm hook: fill `MS_ASM_SOURCES` in `CMakeLists.txt`; each is assembled with
  `tools/toolchain/vasmm68k_mot.exe -Felf -m68020 -devpac` and linked in.
- Do not add `-fno-omit-frame-pointer` (GCC 15.1 ICE in bartman_gcc_support).
- C++ is built `-fno-exceptions -fno-rtti -fno-threadsafe-statics`; no STL, no
  `new`/`delete`, global constructors do not run.
- Debug needs `-fno-lto -fno-whole-program` for **C and C++** (set in CMakeLists);
  otherwise the whole-program link hides ACE symbols from non-LTO C++ objects.
- CMake text writes on Windows are CRLF; the startup-sequence is written via
  Python to guarantee LF.
- Release links with `--wrap=KPrintF` (stock WinUAE lacks Bartman's debug trap).
