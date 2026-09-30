#!/usr/bin/env python3
"""Uji `jawa fmt` atas seluruh basis kode.

Yang diperiksa, untuk setiap berkas `.jw` di proyek:

  1. Format bisa dijalankan. Hasilnya sudah diverifikasi `jawa fmt` sendiri:
     jumlah token sebelum/sesudah harus sama dan hasilnya harus bisa di-parse.
     Galat di sini berarti ada bug di formatter.
  2. **Idempoten**: `fmt(fmt(x)) == fmt(x)`. Ini yang paling penting untuk
     `jawa fmt --tulis`; kalau tidak, setiap commit menghasilkan diff baru.
  3. **Perilaku tidak berubah**: untuk berkas di `examples/`, keluaran
     `jawa run` pada berkas asli dan pada hasil format harus identik. Ini bukti
     terkuat bahwa formatter hanya mengubah jarak.

Berkas di `tests/tes/` tidak diperiksa di titik 3: assertion-nya menulis ke
stdout dan formatnya sendiri boleh berbeda dari gaya proyek.
"""
from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys
import tempfile

AKAR = pathlib.Path(__file__).resolve().parent.parent


def kumpulkan(akar: pathlib.Path) -> list[pathlib.Path]:
    return sorted(p for p in akar.rglob("*.jw") if p.is_file())


def cek_perilaku(jawa: str, asli: pathlib.Path, hasil: str) -> str | None:
    """Bandingkan keluaran `jawa run` sebelum dan sesudah format."""
    sebelum = subprocess.run([jawa, "run", str(asli)], capture_output=True, text=True)
    # Hasil format ditulis di sebelah aslinya supaya impor relatif
    # (`saka "./modul/..."`) tetap ketemu.
    sementara = asli.with_suffix(".cekfmt.jw")
    try:
        sementara.write_text(hasil, encoding="utf-8")
        sesudah = subprocess.run([jawa, "run", str(sementara)], capture_output=True, text=True)
    finally:
        sementara.unlink(missing_ok=True)

    if (sebelum.returncode, sebelum.stdout) == (sesudah.returncode, sesudah.stdout):
        return None
    for a, b in zip(sebelum.stdout.splitlines(), sesudah.stdout.splitlines()):
        if a != b:
            return f"keluaran berubah\n    asli : {a!r}\n    format: {b!r}"
    return f"kode keluar berubah: {sebelum.returncode} -> {sesudah.returncode}"


def main() -> int:
    ap = argparse.ArgumentParser(description="Uji idempoten dan keamanan `jawa fmt`.")
    ap.add_argument("--bawaan", help="Jalur ke binari jawa")
    args = ap.parse_args()

    jawa = args.bawaan or str(AKAR / "build" / "release" / "jawa")
    if not pathlib.Path(jawa).exists():
        print(f"cek_fmt: binari tidak ditemukan: {jawa}", file=sys.stderr)
        return 2

    semua = kumpulkan(AKAR / "examples") + kumpulkan(AKAR / "tests" / "tes")
    if not semua:
        print("cek_fmt: tidak ada berkas .jw untuk diuji", file=sys.stderr)
        return 2

    gagal = 0
    with tempfile.TemporaryDirectory() as tmp:
        tmpdir = pathlib.Path(tmp)
        for f in semua:
            relatif = f.relative_to(AKAR)
            r1 = subprocess.run([jawa, "fmt", str(f)], capture_output=True, text=True)
            if r1.returncode != 0:
                print(f"X {relatif}: format gagal: {r1.stderr.strip()}")
                gagal += 1
                continue
            sekali = r1.stdout

            p1 = tmpdir / "satu.jw"
            p1.write_text(sekali, encoding="utf-8")
            r2 = subprocess.run([jawa, "fmt", str(p1)], capture_output=True, text=True)
            if r2.returncode != 0:
                print(f"X {relatif}: format kedua gagal: {r2.stderr.strip()}")
                gagal += 1
                continue
            if r2.stdout != sekali:
                print(f"X {relatif}: tidak idempoten")
                for i, (a, b) in enumerate(zip(sekali.splitlines(), r2.stdout.splitlines())):
                    if a != b:
                        print(f"    baris {i + 1}:")
                        print(f"      sekali   : {a!r}")
                        print(f"      dua kali : {b!r}")
                        break
                else:
                    print(f"    panjang baris: {len(sekali.splitlines())} -> {len(r2.stdout.splitlines())}")
                gagal += 1
                continue

            rc = subprocess.run([jawa, "fmt", "--cek", str(p1)], capture_output=True, text=True)
            if rc.returncode != 0:
                print(f"X {relatif}: --cek menolak hasil format sendiri")
                gagal += 1
                continue

            if f.is_relative_to(AKAR / "examples"):
                pesan = cek_perilaku(jawa, f, sekali)
                if pesan is not None:
                    print(f"X {relatif}: {pesan}")
                    gagal += 1

    total = len(semua)
    if gagal:
        print(f"cek_fmt: {gagal} dari {total} berkas GAGAL.")
        return 1
    print(f"cek_fmt: {total}/{total} berkas rapi, idempoten, dan perilakunya tidak berubah.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
