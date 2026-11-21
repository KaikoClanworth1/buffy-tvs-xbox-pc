"""The port's built-in list of pixel-shader keys: port/src/btvs_native_shader_keys.h.

    python port/tools/shader_keys.py [shader_keys.bin ...]

Every game that runs notes the key of each shader it had to make in
ShaderCache/shader_keys.bin beside its exe (xboxrecomp/src/kernel/nv2a_native.inc:
type, length, bytes). This gathers the texture-stage ('F') and combiner ('P') keys of
the files given -- with none given: of every shader_keys.bin under this checkout, its
worktrees and GAME/ -- adds them to the ones already in the header, and writes the
header again. `buffy_tvs.exe --prepare-shaders` and every start of the game make the
shaders of that list before the first frame.

Not the vertex-program keys ('V'): those hold the programs themselves, which the game
builds while it runs. They are game code and stay out of the repository; a PC's own
shader_keys.bin keeps them for that PC.

A key is the numbers of a draw's texture-stage set-up (operations and arguments of the
four stages, which stages have a texture): nothing of the game's data.
"""
import glob, os, re, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.dirname(HERE)
ROOT = os.path.dirname(PORT)
HEADER = os.path.join(PORT, 'src', 'btvs_native_shader_keys.h')
KEEP = ('F', 'P')


def records(data):
    pos = 0
    while pos + 5 <= len(data):
        kind = chr(data[pos])
        (n,) = struct.unpack_from('<I', data, pos + 1)
        body = data[pos + 5:pos + 5 + n]
        if len(body) != n:
            break
        yield kind, body
        pos += 5 + n


def from_header():
    if not os.path.exists(HERE) or not os.path.exists(HEADER):
        return b''
    text = open(HEADER, encoding='utf-8').read()
    m = re.search(r'k_btvs_shader_keys\[\]\s*=\s*\{(.*?)\};', text, re.S)
    return bytes(int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})', m.group(1))) if m else b''


def main():
    files = sys.argv[1:]
    if not files:
        main_root = ROOT
        # a worktree lives in <main>/.claude/worktrees/<name>
        parts = ROOT.replace('\\', '/').split('/.claude/worktrees/')
        if len(parts) == 2:
            main_root = parts[0]
        pats = [os.path.join(main_root, 'GAME', 'ShaderCache*', 'shader_keys.bin'),
                os.path.join(main_root, 'port', 'build', '*', 'ShaderCache*', 'shader_keys.bin'),
                os.path.join(main_root, '.claude', 'worktrees', '*', 'port', 'build', '*', 'ShaderCache*', 'shader_keys.bin')]
        for p in pats:
            files += glob.glob(p)
    keys = {}
    for kind, body in records(from_header()):
        keys[(kind, body)] = 'header'
    had = len(keys)
    for f in files:
        try:
            data = open(f, 'rb').read()
        except OSError as e:
            print('skipped', f, e)
            continue
        n = 0
        for kind, body in records(data):
            if kind in KEEP and (kind, body) not in keys:
                keys[(kind, body)] = f
                n += 1
        print('%3d new of %s' % (n, f))
    out = bytearray()
    for (kind, body) in sorted(keys, key=lambda k: (k[0], k[1])):
        out += kind.encode() + struct.pack('<I', len(body)) + body
    lines = []
    for i in range(0, len(out), 20):
        lines.append('    ' + ', '.join('0x%02X' % b for b in out[i:i + 20]) + ',')
    counts = {k: sum(1 for (kind, _) in keys if kind == k) for k in KEEP}
    text = ('/* The pixel-shader keys the port brings with it: written by port/tools/shader_keys.py,\n'
            ' * not by hand (see there). %d texture-stage and %d combiner keys.\n'
            ' * Made into shaders before the first frame (btvs_native.c). */\n'
            'static const unsigned char k_btvs_shader_keys[] = {\n%s\n};\n'
            % (counts['F'], counts['P'], '\n'.join(lines) if lines else '    0'))
    open(HEADER, 'w', encoding='utf-8', newline='\n').write(text)
    print('%d keys in %s (%d were there)' % (len(keys), os.path.relpath(HEADER, ROOT), had))


if __name__ == '__main__':
    main()
