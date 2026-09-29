#!/usr/bin/env python3
"""Periksa sumber C++/dokumentasi dari karakter asing & pola rusak.

Aturan:
  - Hanya izinkan: ASCII, Latin-1 Supplement, Latin Extended, General Punctuation,
    Mathematical Operators, Box Drawing, Javanese Script (U+A980-A9DF).
  - Tolak: CJK, Cyrillic, Arabic, Hiragana/Katakana, Fullwidth, dll.
  - Tolak identifier C++ yang mengandung huruf non-ASCII kecuali di dalam string
    literal (mis. pesan dwibahasa).
"""
import re
import sys
import glob
import os

ALLOW_RANGES = [
    (0x00A0, 0x024F),   # Latin-1 supp, Latin Ext A/B
    (0x2000, 0x206F),   # General punctuation
    (0x2190, 0x21FF),   # Arrows
    (0x2200, 0x22FF),   # Math operators
    (0x2500, 0x257F),   # Box drawing
    (0xA980, 0xA9DF),   # Javanese script
    (0x2070, 0x209F),   # Super/subscript
    (0x2705, 0x2705),   # check mark (status rencana)
    (0x20A0, 0x20BF),   # Currency
    (0x0370, 0x03FF),   # Greek (symbols in docs)
]
PATTERNS = [
    (r'//-[A-Za-z]', 'komentar yang diawali `//-` (biasalah salah ketik)'),
    (r'\bada_galat_placeholder', 'placeholder tertinggal'),
    (r'TODO_REMOVE', 'TODO_REMOVE tertinggal'),
    (r'XXX_', 'nama sementara tertinggal'),
]

def allowed(ch: str) -> bool:
    o = ord(ch)
    if o < 128:
        return True
    for lo, hi in ALLOW_RANGES:
        if lo <= o <= hi:
            return True
    return False

def main() -> int:
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    targets = []
    for pat in ('src/**/*', 'include/**/*', 'tests/**/*.cpp', 'tests/**/*.h',
                'docs/**/*.md', '*.md', 'examples/**/*.jw', 'bench/**/*.jw',
                'fuzz/*.cpp', 'tools/*.py'):
        targets.extend(glob.glob(os.path.join(root, pat), recursive=True))
    masalah = 0
    for f in targets:
        if not os.path.isfile(f):
            continue
        try:
            with open(f, encoding='utf-8') as fh:
                lines = fh.read().split('\n')
        except (UnicodeDecodeError, IsADirectoryError):
            print(f'{f}: BUKAN UTF-8')
            masalah += 1
            continue
        for i, line in enumerate(lines, 1):
            for ch in line:
                if not allowed(ch):
                    print(f'{f}:{i}: karakter terlarang U+{ord(ch):04X} {ch!r}')
                    masalah += 1
                    break
        if os.path.abspath(f).endswith('check_sumber.py'):
            continue
        for pat, ket in PATTERNS:
            for i, line in enumerate(lines, 1):
                if re.search(pat, line):
                    print(f'{f}:{i}: {ket}: {line.strip()[:70]}')
                    masalah += 1
                    break
    if masalah:
        print(f'\n{masalah} masalah ditemukan.')
        return 1
    print('OK: semua berkas bersih.')
    return 0

if __name__ == '__main__':
    sys.exit(main())
