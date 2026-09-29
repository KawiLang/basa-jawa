#!/usr/bin/env python3
"""Jalankan target fuzzing Basa Jawa.

Dua mode:

  --satuan   satu proses per target dengan batas kasus kecil (dipakai ctest;
             hasilnya hanya "tidak crash", bukan bukti kebenaran)
  (default)  campaign lebih panjang per target, memakai `examples/*.jw` sebagai
             korpus seed, PLUS pemeriksaan determinisme

Setiap target menerima `--batas=N` (jumlah kasus) dan `--benih=N` (PRNG), jadi
campaign bisa direproduksi persis. Skrip ini menjalankan campaign yang sama dua
kali dengan benih sama dan menganggap apa pun yang berbeda sebagai kegagalan --
kalau hasilnya tidak reproducible, "lolos" tidak berarti apa-apa.

Tanpa dependensi luar: hanya python3 standar.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys
import time

AKAR = pathlib.Path(__file__).resolve().parent.parent
TARGET_BAWAAN = ["fuzz_lexer", "fuzz_parser", "fuzz_kompilasi", "fuzz_vm", "fuzz_modul"]

# `fuzz_lexer      5000 kasus | lex 2006 gagal | ...`
# Kolom diberi lebar rata (`%8zu`), jadi semua pemisah harus `\s+`, bukan
# satu spasi -- kalau tidak, keluaran yang sebenarnya sah akan ditolak.
POLA_STATISTIK = re.compile(
    r"^(?P<label>\S+)\s+(?P<total>\d+)\s+kasus\s*\|\s*lex\s+(?P<lex>\d+)\s+gagal\s*\|"
    r"\s*parse\s+(?P<parse>\d+)\s+gagal\s*\|\s*kompilasi\s+(?P<kompilasi>\d+)\s*\|"
    r"\s*jalan\s+(?P<jalan>\d+)\s*\|\s*korpus\s+(?P<korpus>\d+)\s*\|"
    r"\s*benih\s+(?P<benih>\d+)$"
)


def cari_korpus() -> list[pathlib.Path]:
    """Berkas `.jw` untuk seed.

    `examples/` dipakai karena isinya program acuan yang sudah benar. Mutasi dari
    sumber yang valid jauh lebih sering menjangkau jalur dalam (parser, VM,
    linker) daripada byte acak.
    """
    return sorted(p for p in (AKAR / "examples").rglob("*.jw") if p.is_file())


def jalankan(bendera: str, argumen: list[str], timeout: int) -> subprocess.CompletedProcess:
    return subprocess.run(
        [bendera, *argumen],
        capture_output=True,
        text=True,
        timeout=timeout,
        cwd=str(AKAR),
    )


def parse_statistik(keluar: str) -> dict[str, int] | None:
    for baris in keluar.splitlines():
        m = POLA_STATISTIK.match(baris.strip())
        if m:
            d = m.groupdict()
            return {k: (v if k == "label" else int(v)) for k, v in d.items()}
    return None


def jalankan_satuan(build: pathlib.Path, target: list[str], batas: int,
                    timeout: int) -> int:
    """Satu proses per target, tanpa korpus, tanpa cek determinisme."""
    gagal = 0
    for nama in target:
        exe = build / nama
        if not exe.exists():
            print(f"{nama}: TIDAK ADA ({exe})", file=sys.stderr)
            gagal += 1
            continue
        t0 = time.monotonic()
        try:
            r = jalankan(str(exe), ["--batas", str(batas)], timeout)
        except subprocess.TimeoutExpired:
            print(f"{nama}: TIMEOUT setelah {timeout}s", file=sys.stderr)
            gagal += 1
            continue
        dt = time.monotonic() - t0
        if r.returncode != 0:
            kasus = " /tmp/jawa_fuzz_kasus.bin"
            print(f"{nama}: KELUAR {r.returncode}\n{r.stderr[-2000:]}\nkasus: {kasus}",
                  file=sys.stderr)
            gagal += 1
            continue
        stat = parse_statistik(r.stdout)
        if stat is None:
            print(f"{nama}: keluaran tidak bisa dibaca\n{r.stdout}", file=sys.stderr)
            gagal += 1
            continue
        print(f"{nama}: {stat['total']} kasus, {stat['kompilasi']} lolos kompilasi, {dt:.1f}s")
    return gagal


def jalankan_campaign(build: pathlib.Path, target: list[str], batas: int,
                      benih: int, timeout: int) -> int:
    korpus = cari_korpus()
    if not korpus:
        print("PERINGATAN: tidak ada berkas .jw untuk korpus seed", file=sys.stderr)
    print(f"korpus seed: {len(korpus)} berkas")
    print(f"batas: {batas} kasus per target\n")

    bermasalah = 0
    for nama in target:
        exe = build / nama
        if not exe.exists():
            print(f"{nama}: TIDAK ADA ({exe})", file=sys.stderr)
            bermasalah += 1
            continue

        extra: list[str] = ["--batas", str(batas)]
        extra += [str(p) for p in korpus]
        if benih:
            extra += ["--benih", str(benih)]

        t0 = time.monotonic()
        try:
            r = jalankan(str(exe), extra, timeout)
        except subprocess.TimeoutExpired:
            print(f"{nama}: TIMEOUT setelah {timeout}s", file=sys.stderr)
            bermasalah += 1
            continue
        dt = time.monotonic() - t0
        if r.returncode != 0:
            print(f"{nama}: KELUAR {r.returncode}\n{r.stderr[-2000:]}\n"
                  f"kasus: /tmp/jawa_fuzz_kasus.bin", file=sys.stderr)
            bermasalah += 1
            continue

        stat1 = parse_statistik(r.stdout)
        if stat1 is None:
            print(f"{nama}: keluaran tidak bisa dibaca\n{r.stdout}", file=sys.stderr)
            bermasalah += 1
            continue

        # Determinisme: dua campaign dengan benih sama harus menghasilkan
        # statistik yang sama persis.
        try:
            r2 = jalankan(str(exe), extra, timeout)
            stat2 = parse_statistik(r2.stdout)
        except subprocess.TimeoutExpired:
            stat2 = None
        if stat2 is None:
            print(f"{nama}: tidak bisa mengulang campaign (determinisme tak terperiksa)")
        elif stat2 != stat1:
            print(f"{nama}: TIDAK DETERMINISTIK\n  {stat1}\n  {stat2}", file=sys.stderr)
            bermasalah += 1
            continue

        laju = f" ({stat1['total'] / dt:.0f} kasus/detik)" if dt > 0 else ""
        print(f"{nama}: {stat1['total']} kasus | lex {stat1['lex']} gagal | "
              f"parse {stat1['parse']} gagal | kompilasi {stat1['kompilasi']} | "
              f"{dt:.1f}s{laju}")

    return bermasalah


def main() -> int:
    sys.stdout.reconfigure(line_buffering=True)
    sys.stderr.reconfigure(line_buffering=True)
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--satuan", action="store_true",
                    help="mode ctest: satu proses per target dengan batas kecil")
    ap.add_argument("--batas", type=int, default=0,
                    help="jumlah kasus per target (0 = bawaan target)")
    ap.add_argument("--benih", type=int, default=0, help="benih PRNG (0 = bawaan target)")
    ap.add_argument("--target", action="append", default=[],
                    help="hanya jalankan target ini (bisa diulang)")
    ap.add_argument("--build", default=str(AKAR / "build" / "fuzz"))
    ap.add_argument("--timeout", type=int, default=1800)
    args = ap.parse_args()

    build = pathlib.Path(args.build)
    target = args.target or TARGET_BAWAAN

    if args.satuan:
        gagal = jalankan_satuan(build, target, args.batas or 400, args.timeout)
        if gagal:
            print(f"\n{gagal} target gagal", file=sys.stderr)
        return 1 if gagal else 0

    bermasalah = jalankan_campaign(build, target, args.batas or 3000, args.benih,
                                  args.timeout)
    if bermasalah:
        print(f"\n{bermasalah} target bermasalah", file=sys.stderr)
    return 1 if bermasalah else 0


if __name__ == "__main__":
    sys.exit(main())
