#!/usr/bin/env python3
"""Copy workspace dependencies into this project; never overwrite differing files.

Run once from the existing D:/Amiga workspace: py tools/vendor_local.py
The copied dependencies are private/local and ignored by git.
"""
import hashlib
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parent


def copy(source, destination):
    if source.is_dir():
        for child in source.iterdir():
            if child.name not in {'.git', '__pycache__'} and not child.name.startswith('build'):
                copy(child, destination / child.name)
        return
    if not source.is_file():
        raise FileNotFoundError(source)
    if destination.exists():
        if hashlib.sha256(source.read_bytes()).digest() != hashlib.sha256(destination.read_bytes()).digest():
            raise RuntimeError(f'Refusing to overwrite differing file: {destination}')
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)


def main():
    copy(WORKSPACE / 'tools/AmigaCMakeCrossToolchains', ROOT / 'tools/AmigaCMakeCrossToolchains')
    bin_source = WORKSPACE / 'tools/amiga-debug/extension/bin/win32'
    bin_dest = ROOT / 'tools/toolchain'
    copy(bin_source / 'opt', bin_dest / 'opt')
    for name in ('elf2hunk.exe', 'exe2adf.exe', 'vasm-LICENSE'):
        copy(bin_source / name, bin_dest / name)
    copy(WORKSPACE / 'breakout/tools/vasmm68k_mot.exe', bin_dest / 'vasmm68k_mot.exe')
    copy(ROOT / 'build/_deps/bartman_gcc_support-src', ROOT / 'tools/bartman_gcc_support')
    reference = ROOT / 'reference/moonshard'
    for name in ('amiga_asm', 'libmoon_assets', 'LICENSE', 'README.md'):
        copy(WORKSPACE / 'moonshard/moonstone-main' / name, reference / 'moonstone-main' / name)
    copy(WORKSPACE / 'moonshard/tools/moghunks.py', reference / 'tools/moghunks.py')
    for name in ('mechanics.c', 'mechanics.h', 'planar_contact.c', 'planar_contact.h'):
        copy(WORKSPACE / 'moonshard' / name, reference / name)
    for disk in 'ABC':
        name = f'Moonstone (Mindscape) {disk}.adf'
        copy(WORKSPACE / 'moonshard' / name, ROOT / 'reference/disks' / name)
    print('Local toolchain, assembler, support library and reference data copied; ACE is shared.')


if __name__ == '__main__':
    main()
