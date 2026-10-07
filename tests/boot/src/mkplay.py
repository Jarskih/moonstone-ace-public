"""Build tests/boot/play_*.txt from their .mk sources (the play scripts are long and mostly relative timing, so they are written
with relative delays and expanded here):

  py tests/boot/src/mkplay.py                      regenerate every play_*.txt next to this directory
  py tests/boot/src/mkplay.py play_map.mk out.txt  one file

Source syntax (everything else is copied as it is, so plain `frame N ...` / `wait ...` / comment lines work):
  +N <command>        N frames after the previous event of the segment; the absolute `frame` number is generated. A line `wait ...`
                      starts a new segment (frames count from the barrier's release, docs/AUTOPLAY.md), `@SET N` sets the clock.
  @PRE                the boot prefix: intro skip, Select Knight, first knight, fire = keep the name (map ready ~600 frames later)
  @click X Y [N]      poke the cursor to X,Y, 25 frames later `joy1 fire pulse N` (default 8): a click on a shop / town screen
  @hold joy1 up N     `joy1 up` now, `joy1 none` N frames later
"""
import os
import re
import sys

PRE = '''# --- boot to the map: intro skip, Select Knight, first knight, keep the preset name (fire = OK) ---
frame 300 key SPACE tap
wait input 10 max 3000
frame 100 joy1 down pulse
frame 130 joy1 down pulse
frame 160 joy1 down pulse
frame 230 joy1 fire pulse
frame 730 joy1 fire pulse
frame 1030 joy1 fire pulse
# --- the map (about 600 frames after the last press) ---'''


def build(text):
    lines = text.replace('@PRE', PRE).split('\n')
    res = []
    cur = 1030
    for l in lines:
        m = re.match(r'^\+(\d+) (.*)$', l)
        if m:
            cur += int(m.group(1))
            cmd = m.group(2)
            mc = re.match(r'^@click (\d+) (\d+)(?: (\d+))?$', cmd)
            if mc:
                res.append('frame %d poke cursorx %s' % (cur, mc.group(1)))
                res.append('frame %d poke cursory %s' % (cur, mc.group(2)))
                cur += 25
                res.append('frame %d joy1 fire pulse %s' % (cur, mc.group(3) or 8))
            elif cmd.startswith('@hold '):
                _, port, d, n = cmd.split()
                res.append('frame %d %s %s' % (cur, port, d))
                cur += int(n)
                res.append('frame %d %s none' % (cur, port))
            else:
                res.append('frame %d %s' % (cur, cmd))
            continue
        if l.startswith('wait '):
            cur = 0
        if l.strip().startswith('@SET'):
            cur = int(l.split()[1])
            continue
        res.append(l)
    return '\n'.join(res).rstrip() + '\n'


def main(argv):
    here = os.path.dirname(os.path.abspath(__file__))
    if len(argv) == 2:
        pairs = [(argv[0], argv[1])]
    else:
        pairs = [(os.path.join(here, f), os.path.join(here, '..', f[:-3] + '.txt')) for f in sorted(os.listdir(here)) if f.endswith('.mk')]
    for src, out in pairs:
        with open(out, 'w', newline='\n') as f:
            f.write(build(open(src).read()))
        print('wrote', os.path.normpath(out))


if __name__ == '__main__':
    main(sys.argv[1:])
