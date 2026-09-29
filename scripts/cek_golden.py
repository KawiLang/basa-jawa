#!/usr/bin/env python3
"""Uji emas (golden test): bandingkan keluaran `jawa run` dengan berkas .out.

Setiap `examples/NAMA.jw` harus menghasilkan persis isi `tests/golden/NAMA.out`.
Skrip ini adalah pemeriksaan KELUARAN UTAMA untuk Fase 3: front-end (lexer+parser)
sudah diuji terpisah, di sini yang diuji adalah bytecode + VM + GC + stdlib.

Contoh yang memang belum didukung (lihat STATUS.md) dikecualikan lewat
JAWA_GOLDEN_SKIP dan WAJIB disertai alasan.
"""
import argparse
import os
import subprocess
import sys

AKAR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JAWA = os.path.join(AKAR, 'build', 'release', 'jawa')
GOLDEN = os.path.join(AKAR, 'tests', 'golden')
CONTOH = os.path.join(AKAR, 'examples')

# Contoh yang kinerjanya masih belum selesai (lihat STATUS.md "Yang BELUM").
# Tidak ada contoh yang dilewati: 11/11 contoh acuan harus menghasilkan keluaran
# persis. Kalau sebuah contoh memang belum didukung, tambahkan di sini BESERTA
# alasannya (lihat STATUS.md) -- jangan dihapus diam-diam.
SKIP: dict[str, str] = {}


def main() -> int:
    ap = argparse.ArgumentParser(description='Uji emas keluaran Basa Jawa')
    ap.add_argument('--gc-stress', action='store_true',
                    help='Jalankan tiap contoh dengan --gc-stress (koleksi tiap alokasi)')
    ap.add_argument('--bawaan', default=os.path.join(AKAR, 'build', 'release', 'jawa'),
                    help='Jalur ke binari jawa')
    args = ap.parse_args()
    jawa = args.bawaan
    if not os.path.exists(jawa):
        print(f'ERROR: {jawa} tidak ada. Build dulu: cmake --build build/release')
        return 1
    if not os.path.isdir(CONTOH):
        print(f'ERROR: {CONTOH} tidak ada.')
        return 1

    nama_terdaftar = sorted(f[:-3] for f in os.listdir(CONTOH) if f.endswith('.jw'))
    gagal = 0
    lulus = 0
    dilewati = 0

    for nama in nama_terdaftar:
        if nama in SKIP:
            print(f'[LEWAT] {nama:12s} {SKIP[nama]}')
            dilewati += 1
            continue
        sumber = os.path.join(CONTOH, f'{nama}.jw')
        harap = os.path.join(GOLDEN, f'{nama}.out')
        if not os.path.exists(harap):
            print(f'[GAGAL] {nama:12s} berkas خرج `tests/golden/{nama}.out` belum ada')
            gagal += 1
            continue
        perintah = [jawa, 'run']
        if args.gc_stress:
            perintah.append('--gc-stress')
        perintah.append(sumber)
        hasil = subprocess.run(perintah, capture_output=True, text=True, timeout=900)
        dengan = open(harap, encoding='utf-8').read()
        if hasil.stdout == dengan:
            print(f'[ OK  ] {nama:12s} {len(dengan.splitlines())} baris')
            lulus += 1
        else:
            gagal += 1
            print(f'[GAGAL] {nama:12s} keluaran beda')
            print(f'         harap : {dengan!r}')
            print(f'         nyata : {hasil.stdout!r}')
            if hasil.stderr.strip():
                print('         stderr: ' + hasil.stderr.strip().replace('\n', '\n                '))

    total = lulus + gagal + dilewati
    print()
    mode = ' (--gc-stress)' if args.gc_stress else ''
    print(f'{lulus}/{total} contoh emas cocok{mode} ({dilewati} dilewati, {gagal} gagal).')
    return 0 if gagal == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
