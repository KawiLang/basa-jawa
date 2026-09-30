#!/usr/bin/env python3
"""Pastikan tidak ada kode Basa Jawa di repo yang salah SECARA DIAM.

Tahap 13 menambah dua diagnosa di lexer:
  L011 (galat)      pengenal di awal pernyataan diikuti token yang mustahil
  L012 (peringatan) pengenal di awal pernyataan yang persis kata dari
                   bahasa lain (`return`, `function`, `let`, ...)

Keduanya menangkap kelas kesalahan yang selama ini lolos tanpa suara --
khususnya kode yang TERLIHAT jalan padahal bukan Basa Jawa. Contoh nyata yang
tertangkap: `examples/kleru.jw` menulis `} intrigasan {` (harusnya `pungkasan`),
dan `tests/tes/upvalue.tes.jw` menulis `lempar "ora";` (padanannya `uncal`) di
dalam `nalika (salah)` sehingga jalur `tangkep`-nya tidak pernah dieksekusi.

Script ini menutup lingkarannya: seluruh `.jw` di repo harus bebas L011/L012.
Kalau muncul, itu regresi -- perbaiki kodenya, jangan whitelist di sini.
"""
import glob
import os
import subprocess
import sys

JAWA = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'build', 'release', 'jawa')
KODE = ('L011', 'L012')


def main() -> int:
    if not os.path.exists(JAWA):
        print(f'ERROR: {JAWA} tidak ada. Build dulu dengan `cmake --build build/release`.',
              file=sys.stderr)
        return 1
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    pola = ('examples/**/*.jw', 'tests/tes/**/*.jw', 'bench/**/*.jw', 'docs/**/*.jw')
    berkas = []
    for p in pola:
        berkas.extend(glob.glob(os.path.join(root, p), recursive=True))
    berkas = sorted(set(b for b in berkas if os.path.isfile(b)))

    masalah = 0
    for f in berkas:
        with open(f, encoding='utf-8') as fh:
            src = fh.read()
        hasil = subprocess.run([JAWA, 'cek'], input=src, capture_output=True, text=True)
        for baris in hasil.stderr.splitlines():
            if any(k in baris for k in KODE):
                rel = os.path.relpath(f, root)
                print(f'{rel}: {baris.strip()}')
                masalah += 1
    print()
    if masalah:
        print(f'{masalah} diagnosa L011/L012 di {len(berkas)} berkas .jw -- '
              f'kode ini bukan Basa Jawa (lihat src/lex/pinjaman.def).')
        return 1
    print(f'OK: {len(berkas)} berkas .jw bebas L011/L012.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
