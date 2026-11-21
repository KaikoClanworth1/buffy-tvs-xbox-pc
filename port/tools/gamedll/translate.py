"""Default.xbe + final.map -> the translated game code, in one program.

  python translate.py <Default.xbe> <final.map> <manual_functions.json> <work folder> [--dll <btvs_game_tls.h>]

The same steps as port/tools/regen.sh, in the same order and with the same
tools, but every file it reads or writes besides the three inputs is under
<work folder> (regen.sh keeps its in-between files in the source tree), and
everything runs in this one process. That makes it fit for freezing into a
single exe for the player's PC, which has no Python.

  <work folder>/gen             the translated code (what regen.sh puts in port/src/recomp/gen)
  <work folder>/translate.log   what the tools printed

--dll <btvs_game_tls.h>: also make the code fit the game DLL (see
docs/install-build.md): the header's thread-local registers are replaced by
the given file's macros (made from the exe's link map by patch_header.py on
the developer's PC and shipped with that exe), and the memory offset becomes
the DLL's own copy.

Progress goes to stdout as lines "STEP <n>/<of> <what>", flushed, for the
launcher. Exit code 0 only when every step worked.
"""
import io
import json
import os
import re
import runpy
import shutil
import sys
import time
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
# frozen (PyInstaller): the tools are inside the bundle; otherwise in the source tree
BUNDLE = getattr(sys, '_MEIPASS', None)
if BUNDLE:
    sys.path[:0] = [BUNDLE, os.path.join(BUNDLE, 'port_tools')]
else:
    sys.path[:0] = [os.path.normpath(os.path.join(HERE, '..', '..', '..', 'xboxrecomp')),
                    os.path.normpath(os.path.join(HERE, '..'))]

STEPS = 9
_step = 0
_real_out = sys.stdout


def step(what):
    global _step
    _step += 1
    _real_out.write('STEP %d/%d %s\n' % (_step, STEPS, what))
    _real_out.flush()


def run(module, *args, path=None):
    """Run a tool as `python -m module args` would, in this process."""
    old = sys.argv
    sys.argv = [module] + [str(a) for a in args]
    try:
        if path:
            runpy.run_path(path, run_name='__main__')
        else:
            runpy.run_module(module, run_name='__main__', alter_sys=True)
    except SystemExit as e:
        if e.code not in (None, 0):
            raise RuntimeError('%s ended with %r' % (module, e.code))
    finally:
        sys.argv = old


def port_tool(name):
    return os.path.join(BUNDLE, 'port_tools', name) if BUNDLE else os.path.join(HERE, '..', name)


def patch_for_dll(gen, tls_header):
    """recomp_types.h for the game DLL: the same change patch_header.py makes."""
    shutil.copyfile(tls_header, os.path.join(gen, 'btvs_game_tls.h'))
    p = os.path.join(gen, 'recomp_types.h')
    s = open(p, encoding='utf-8', errors='surrogateescape', newline='').read()
    if 'btvs_game_tls.h' in s:
        return
    s, n = re.subn(r'(?m)^extern RECOMP_TLS ([^;]*;)', r'/* in the exe: \1 */', s)
    if n < 10:
        raise RuntimeError('recomp_types.h: only %d thread-local declarations found' % n)
    d = 'extern ptrdiff_t g_xbox_mem_offset;'
    if d not in s:
        raise RuntimeError('recomp_types.h: no memory offset declaration')
    s = s.replace(d, 'extern ptrdiff_t btvs_game_mem_offset;\n#define g_xbox_mem_offset btvs_game_mem_offset')
    for d in ('extern uint32_t g_xbox_code_lo;', 'extern uint32_t g_xbox_code_hi;'):
        if d not in s:
            raise RuntimeError('recomp_types.h: no "%s"' % d)
        s = s.replace(d, d.replace('extern ', 'extern __declspec(dllimport) '))
    s, k = re.subn(r'(?m)^extern volatile (uint\d+_t g_icall_)', r'extern __declspec(dllimport) volatile \1', s)
    if k != 3:
        raise RuntimeError('recomp_types.h: %d call-trace declarations, expected 3' % k)
    inc = '#include <stddef.h>'
    if inc not in s:
        raise RuntimeError('recomp_types.h: no <stddef.h>')
    s = s.replace(inc, inc + '\n#include "btvs_game_tls.h"', 1)
    open(p, 'w', encoding='utf-8', errors='surrogateescape', newline='').write(s)


def main():
    args = sys.argv[1:]
    tls = None
    if '--dll' in args:
        i = args.index('--dll')
        tls = os.path.abspath(args[i + 1])
        del args[i:i + 2]
    if len(args) != 4:
        sys.stderr.write(__doc__)
        return 2
    xbe, mapf, manual, work = (os.path.abspath(a) for a in args)
    os.makedirs(work, exist_ok=True)
    gf, dis, fid, abi, gen = (os.path.join(work, d) for d in ('game', 'disasm', 'func_id', 'abi', 'gen'))
    for d in (gf, dis, fid, abi, gen):
        shutil.rmtree(d, ignore_errors=True)        # a stopped run leaves halves: always from the start
        os.makedirs(d)
    log = open(os.path.join(work, 'translate.log'), 'w', encoding='utf-8', errors='replace')
    sys.stdout = sys.stderr = log
    t0 = time.time()
    try:
        # regen.sh gives the tools a copy named default.xbe
        x = os.path.join(gf, 'default.xbe')
        shutil.copyfile(xbe, x)
        analysis = os.path.join(gf, 'default_analysis.json')
        funcs = os.path.join(dis, 'functions.json')
        cnames = os.path.join(gf, 'map_cnames.json')

        step('reading the game program')
        run('tools.xbe_parser', x, '--json', analysis)
        step('reading the names in final.map')
        run('map_seeds', mapf, analysis, gf, path=port_tool('map_seeds.py'))
        step('finding the game\'s functions')
        run('tools.disasm', x, '--force', '-o', dis, '--analysis-json', analysis,
            '--seed-functions', os.path.join(gf, 'map_seeds.json'))
        # DOLBY is DSP microcode, not x86.
        f = [e for e in json.load(open(funcs)) if e['section'] != 'DOLBY']
        json.dump(f, open(funcs, 'w'), indent=1)
        step('naming them')
        run('tools.ghidra_naming.merge_names', '--names-json', cnames, '--out', os.path.join(gf, 'applied_names.json'),
            '--functions-json', funcs, '--apply')
        run('fix_bounds', funcs, cnames, path=port_tool('fix_bounds.py'))
        step('telling the game\'s code from the Xbox libraries')
        run('tools.func_id', x, '--functions', funcs, '--strings', os.path.join(dis, 'strings.json'),
            '--xrefs', os.path.join(dis, 'xrefs.json'), '--output', fid)
        step('working out how functions are called')
        run('tools.abi_analysis', x, '--disasm-dir', dis, '--func-id-dir', fid, '--output-dir', abi)
        step('translating to C')
        run('tools.recomp', x, '--all', '--split', '1000', '--game-name', 'Buffy the Vampire Slayer',
            '--gen-dir', gen, '-o', os.path.join(work, 'recomp_out'), '--disasm-dir', dis, '--func-id-dir', fid, '--abi-dir', abi,
            '--manual-functions', manual)
        shutil.copyfile(manual, os.path.join(gen, 'manual_functions.applied.json'))
        for need in ('recomp_types.h', 'recomp_funcs.h', 'recomp_dispatch.c'):
            if not os.path.isfile(os.path.join(gen, need)):
                raise RuntimeError('%s was not written' % need)
        step('fitting the code to the game DLL' if tls else 'done translating')
        if tls:
            patch_for_dll(gen, tls)
        n = len([f for f in os.listdir(gen) if f.endswith('.c')])
        if n < 10:
            raise RuntimeError('only %d C files were written' % n)
        print('translate.py: %d C files in %.0f s' % (n, time.time() - t0))
        step('done: %d files' % n)
        return 0
    except Exception:
        traceback.print_exc(file=log)
        log.flush()
        _real_out.write('FAILED at step %d (see translate.log)\n' % _step)
        _real_out.flush()
        return 1
    finally:
        log.flush()


if __name__ == '__main__':
    sys.exit(main())
