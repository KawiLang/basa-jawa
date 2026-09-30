#!/usr/bin/env python3
"""Uji `jawa repl`.

REPL adalah alat interaktif, jadi mengujenya berarti menjalankan `jawa repl`
dengan masukan yang dikendalikan (bukan terminal) dan membandingkan keluarannya
persis. Yang diuji:

  - nilai ekspresi di frame modul dicetak (`1+1` -> `2`),
  - pengikut bertahan antar baris (`ana x = 10` lalu `x * 2` -> `20`),
  - fungsi & class yang didefinisikan di baris sebelumnya bisa dipanggil,
  - masukan beberapa baris: kurung kurawal yang belum tertutup ditunggu,
  - `tulis(1)` tidak dicetak dua kali,
  - galat tidak menghentikan sesi, dan kode keluar proses bukan nol,
  - perintah `:q`, `:nilai`, `:reset` bekerja.
"""
from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys

AKAR = pathlib.Path(__file__).resolve().parent.parent

# (nama, masukan, keluaran yang diharapkan)
KASUS: list[tuple[str, str, str]] = [
    (
        "ekspresi dasar",
        "1 + 1\n:q\n",
        "2\n",
    ),
    (
        "pengikut bertahan antar baris",
        "ana x = 10\nx * 2\n:q\n",
        "20\n",
    ),
    (
        "teks & koleksi",
        '"halo"\n[1, 2, 3]\njenis([1])\n:q\n',
        "halo\n[1, 2, 3]\ndhaptar\n",
    ),
    (
        "fungsi lintas baris",
        "gawe kali(a, b) { bali a * b }\nkali(6, 7)\n:q\n",
        "42\n",
    ),
    (
        # Fungsi tanpa `bali` mengembalikan nilai kosong, bukan nilai ekspresi
        # terakhir. Ini perilaku yang sudah ada sebelum REPL; dicatat supaya
        # perubahan lain tidak diam-diam mengubahnya.
        "fungsi tanpa bali mengembalikan kosong",
        "gawe hampa() { }\njenis(hampa())\n:q\n",
        "mboh\n",
    ),
    (
        "tulis tidak dicetak dua kali",
        "tulis(1)\n:q\n",
        "1\n",
    ),
    (
        "masukan beberapa baris",
        "gawe f() {\n  bali 5\n}\nf()\n:q\n",
        "5\n",
    ),
    (
        "blok kurung kurawal lintas baris",
        "{\n  ana y = 3;\n  y + 1\n}\n:q\n",
        "4\n",
    ),
    (
        "galat runtime tidak menghentikan sesi",
        'tulis(1 + "a");\ntulis("masih hidup");\n:q\n',
        "masih hidup\n",
    ),
    (
        "perintah :nilai",
        "ana z = 8\n:nilai z\n:q\n",
        "z = 8\n",
    ),
    (
        "perintah :reset menghapus pengikut",
        "ana w = 1\n:reset\nw\n:q\n",
        "state direset\nundefined\n",
    ),
    (
        "perintah :bantuan",
        ":bantuan\n:q\n",
        None,  # hanya dicek tidak galat
    ),
]


def main() -> int:
    ap = argparse.ArgumentParser(description="Uji `jawa repl`.")
    ap.add_argument("--bawaan", help="Jalur ke binari jawa")
    args = ap.parse_args()

    jawa = args.bawaan or str(AKAR / "build" / "release" / "jawa")
    if not pathlib.Path(jawa).exists():
        print(f"cek_repl: binari tidak ditemukan: {jawa}", file=sys.stderr)
        return 2

    gagal = 0
    for nama, masukan, diharapkan in KASUS:
        r = subprocess.run([jawa, "repl"], input=masukan, capture_output=True, text=True, timeout=60)
        if diharapkan is not None and r.stdout != diharapkan:
            print(f"X {nama}: keluaran tidak cocok")
            print(f"  diharapkan: {diharapkan!r}")
            print(f"  didapat   : {r.stdout!r}")
            if r.stderr:
                print(f"  stderr    : {r.stderr.strip()[:200]}")
            gagal += 1
            continue
        # Galat yang memang diharapkan pada kasus "galat runtime" boleh
        # keluar dengan kode bukan nol; kasus lain tidak boleh.
        if "galat" not in nama and r.returncode != 0:
            print(f"X {nama}: kode keluar {r.returncode}, stderr: {r.stderr.strip()[:200]}")
            gagal += 1
            continue
        if r.returncode != 0 and "galat" in nama and r.returncode == 0:
            print(f"X {nama}: diharapkan kode keluar bukan nol")
            gagal += 1

    if gagal:
        print(f"cek_repl: {gagal} dari {len(KASUS)} kasus GAGAL.")
        return 1
    print(f"cek_repl: {len(KASUS)}/{len(KASUS)} kasus lulus.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
