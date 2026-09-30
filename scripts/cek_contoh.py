#!/usr/bin/env python3
"""Verifikasi contoh acuan Bagian 11 setidaknya ter-parse bersih oleh front-end.

Catatan jujur: `jawa cek` hanya menjalankan lexer+parser. Menjalankan program
(VM) belum tersedia — lihat STATUS.md.
"""
import subprocess
import sys
import os

JAWA = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'build', 'release', 'jawa')

CONTOH = {
    'halo': '''tetep jeneng = "Budi";
tulis(`Halo, ${jeneng}!`);
''',
    'fizzbuzz': '''kanggo (ana i = 1; i <= 15; i++) {
  yen (i % 15 === 0) { tulis("FizzBuzz"); }
  liyane yen (i % 3 === 0) { tulis("Fizz"); }
  liyane yen (i % 5 === 0) { tulis("Buzz"); }
  liyane { tulis(i); }
}
''',
    'closure': '''gawe cacahan() {
  ana n = 0;
  bali () => ++n;
}
tetep c = cacahan();
tulis(c(), c(), c());
''',
    'golongan': '''golongan Kewan {
  #jeneng;
  wiwit(jeneng) { iki.#jeneng = jeneng; }
  nampa jeneng() { bali iki.#jeneng; }
  swara() { tulis(`${iki.#jeneng} ngeluarke swara`); }
}
golongan Kucing turunan Kewan {
  swara() { induk.swara(); tulis("meong!"); }
}
tetep k = anyar Kucing("Tom");
k.swara();
tulis(k.jeneng);
''',
    'generator': '''gawe* cacah(n) {
  kanggo (ana i = 1; i <= n; i++) metokake i;
}
tulis([...cacah(5)]);
''',
    'asinkron': '''mengko gawe ambil(x) {
  enteni Wektu.tundha(10);
  bali x * 2;
}
mengko gawe utama() {
  tetep a = enteni ambil(21);
  tulis(a);
}
utama();
tulis("dhisik");
''',
    'cocog': '''gawe deskripsi(x) {
  bali cocog (x) {
    kasus 0 => "nol",
    kasus [a, b] => `pasangan ${a},${b}`,
    kasus {jeneng, umur} yen umur >= 17 => `${jeneng} wis dewasa`,
    kasus _ => "liyane"
  };
}
tulis(deskripsi(0));
tulis(deskripsi([1, 2]));
tulis(deskripsi({jeneng: "Sari", umur: 20}));
tulis(deskripsi("x"));
''',
    'pipeline': 'tulis(5 |> (x => x + 1) |> (x => x * 2));\n',
    'kleru': '''coba {
  ana o = kosong;
  tulis(o.x);
} tangkep (e) {
  tulis(e.jeneng, "-", e.pesen);
} pungkasan {
  tulis("Rampung");
}
''',
    'tipe': '''gawe tambah(a: angka, b: angka): angka { bali a + b; }
tulis(tambah(2, 3));
coba { tambah("2", 3); } tangkep (e) { tulis(e.jeneng); }
''',
    'angka_jawa': '''tulis(StdAksara.angka_jawa(21));
''',
}


def main() -> int:
    if not os.path.exists(JAWA):
        print(f'ERROR: {JAWA} tidak ada. Build dulu dengan `cmake --build build/release`.')
        return 1
    gagal = 0
    for nama, src in sorted(CONTOH.items()):
        hasil = subprocess.run([JAWA, 'cek'], input=src, capture_output=True, text=True)
        ok = hasil.returncode == 0
        status = 'OK  ' if ok else 'GAGAL'
        print(f'[{status}] {nama:12s} {hasil.stdout.strip().splitlines()[0] if hasil.stdout.strip() else ""}')
        if not ok:
            gagal += 1
            print('       ' + hasil.stderr.strip().replace('\n', '\n       '))
    print()
    print(f'{len(CONTOH) - gagal}/{len(CONTOH)} contoh acuan ter-parse bersih oleh lexer+parser.')
    return 0 if gagal == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
