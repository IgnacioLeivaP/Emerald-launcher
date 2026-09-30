#!/usr/bin/env python3
"""Checks the Spanish table in src/i18n.cpp against the code:
every tr("...") string must have a translation, and each translation must
use the same printf conversions (%d, %s...) in the same order.
Run from the repository root; exits non-zero on problems (used by CI)."""
import glob
import re
import sys

SRC = 'src'
spec = re.compile(r'%[-+ #0]*\d*(?:\.\d+)?[a-zA-Z]')
lit = r'"((?:[^"\\]|\\.)*)"'

keys = set()
for path in glob.glob(f'{SRC}/*.cpp'):
    for m in re.finditer(r'\btr\(' + lit + r'\)', open(path, encoding='utf-8').read()):
        keys.add(m.group(1))
# Strings translated through variables (shader names, button names).
keys.update(['None (sharp pixels)', 'Smooth (ScaleFX-9x)', 'Scanlines', 'CRT (scanlines + vignette)',
             'LCD Grid (handheld)', 'Bloom (glow on brights)', 'Up', 'Down', 'Left', 'Right', 'Start', 'Select',
             'db.json order', 'A-Z', 'Year', 'Platform', 'Recently played', 'Most played'])

table = {}
problems = []
for m in re.finditer(r'\{' + lit + r',\s*' + lit + r'\}', open(f'{SRC}/i18n.cpp', encoding='utf-8').read()):
    en, es = m.group(1), m.group(2)
    if en in table:
        problems.append(f'duplicate entry: {en!r}')
    table[en] = es
    if spec.findall(en.split('##')[0]) != spec.findall(es):
        problems.append(f'format mismatch: {en!r} -> {es!r}')

for k in sorted(keys - set(table)):
    problems.append(f'no Spanish text for: {k!r}')

for p in problems:
    print(p)
print(f'{len(keys)} strings, {len(table)} translations, {len(problems)} problems')
sys.exit(1 if problems else 0)
