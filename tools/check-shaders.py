#!/usr/bin/env python3
"""Validates every GLSL shader in the C sources as GLSL ES 3.00 (what the
Switch compiles) with glslangValidator. Shaders are C string literals:

    static const char *NAME =
        GLSL_VERSION
        "line\\n"
        ...;

Names ending in VS are vertex shaders, the rest fragment shaders.
Run from the repository root; exits non-zero if any shader fails (CI)."""
import glob
import os
import re
import subprocess
import sys
import tempfile

lit = re.compile(r'"((?:[^"\\]|\\.)*)"')
failed = 0
count = 0
with tempfile.TemporaryDirectory() as tmp:
    for path in sorted(glob.glob('src/*.c')):
        name, parts = None, []
        for line in open(path, encoding='utf-8'):
            if name is None:
                m = re.match(r'\s*static const char \*(\w+)\s*=\s*$', line)
                if m:
                    name, parts = m.group(1), []
                continue
            parts.extend(lit.findall(line))
            if line.rstrip().endswith(';'):
                text = ''.join(parts).encode().decode('unicode_escape')
                stage = 'vert' if name.endswith('VS') or name.startswith('s_vert') else 'frag'
                out = os.path.join(tmp, f'{os.path.basename(path)}_{name}.{stage}')
                with open(out, 'w') as f:
                    f.write('#version 300 es\nprecision highp float;\n' + text)
                res = subprocess.run(['glslangValidator', out], capture_output=True, text=True)
                count += 1
                if res.returncode != 0:
                    failed += 1
                    print(f'FAIL {path}: {name}\n{res.stdout}{res.stderr}')
                name = None
print(f'{count} shaders, {failed} failed')
sys.exit(1 if failed else 0)
