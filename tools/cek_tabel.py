#!/usr/bin/env python3
"""Periksa invarian tabel data: `src/lex/keywords.def`, `src/lex/pinjaman.def`,
`src/support/messages.def`.

Tiga berkas itu adalah sumber kebenaran tunggal (keyword, kata dari bahasa lain,
pesan diagnostik). Tidak ada yang memverifikasi isinya, jadi kesalahan di sini
tidak muncul sebagai galat compile -- hanya sebagai perilaku yang aneh:

  - kode pesan duplikat  -> `Lexer::diagnostik("L011", ...)` diam-diam memakai
                           pesan yang salah, tanpa satu pun tanda Compile gagal;
  - awalan kode != jenis -> `KleruToken [S001]` untuk pesan lexer, kategori
                           yang salah saat pesan disaring according to kind;
  - entri pinjaman mati  -> kata yang sudah jadi kata kunci Basa Jawa, atau
                           padanan yang tidak ada di Basa Jawa, jadi sarannya
                           menunjuk ke kata yang tidak ada.

Syarat-syarat ini diuji sebagai test, tapi hanya pada handful entri yang dipilih
tangan. Pemeriksaan di sini menutup seluruh tabel.
"""
import re
import sys
import os

# Padanan fungsi global: nama yang didaftarkan pustaka standar.
FUNGSI_GLOBAL = {
    'tulis', 'pratelas', 'jenis', 'jinis', 'dawa', 'tambah', 'saring',
    'buka', 'baca', 'tutup', 'hapus', 'ada', 'dadi_direktori',
    'hapus_direktori', 'ada_direktori', 'kaca', 'tuduh', 'krip',
}

# Awalan kode -> DiagKind yang boleh dipakai di `JAWA_MSG`.
# `S` dipakai dua jenis: sintaks (S001 dst) & scope (S401 dst).
JENIS_DARI_AWALAN = {
    'C': {'CLI'},
    'L': {'Lexer'},
    'S': {'Sintaks', 'Scope'},
    'T': {'Tipe'},
    'R': {'Runtime'},
    'I': {'Internal'},
}


def baca_nama_tipe(path):
    """Nama tipe yang benar-benar ada di Basa Jawa, diambil dari fungsi
    `jenis_nilai()` di src/rt/string.cpp -- bukan daftar yang ditulis tangan,
    supaya kalau ada tipe baru pemeriksaannya ikut benar."""
    with open(path, encoding='utf-8') as fh:
        isi = fh.read()
    awal = isi.find('nama_jenis(')
    if awal < 0:
        return set()
    # Ambil hanya badan fungsi: sampai kurung kurawal penutup di kolom 0.
    akhir = isi.find('\n}', awal)
    badan = isi[awal:akhir if akhir > 0 else len(isi)]
    return set(re.findall(r'return "([a-z]+)";', badan))


def baca_java_kunci(path):
    """Kumpulkan semua ejaan kata kunci (ngoko + krama)."""
    hasil = set()
    pola = re.compile(r'JAWA_KEYWORD\("([^"]+)",\s*"([^"]*)"')
    with open(path, encoding='utf-8') as fh:
        for baris in fh:
            m = pola.match(baris)
            if not m:
                continue
            hasil.add(m.group(1))
            if m.group(2):
                hasil.add(m.group(2))
    return hasil


def cek_keyword(path, masalah):
    """`keywords.def`: satu kata tidak boleh punya dua ejaan (ngoko == krama),
    dan tidak boleh ejaan yang sama untuk dua baris berbeda."""
    with open(path, encoding='utf-8') as fh:
        baris = [ln for ln in fh if ln.startswith('JAWA_KEYWORD(')]
    ejaan = {}
    for ln in baris:
        m = re.match(r'JAWA_KEYWORD\("([^"]+)",\s*"([^"]*)"', ln)
        if not m:
            masalah.append(f'{path}: baris JAWA_KEYWORD tidak bisa dibaca: {ln.strip()[:60]}')
            continue
        if m.group(1) == m.group(2):
            masalah.append(f'{path}: kata kunci `{m.group(1)}` punya ngoko == krama')
        kunci = (m.group(1), m.group(2))
        if kunci in ejaan:
            masalah.append(f'{path}: baris ganda {kunci}')
        ejaan[kunci] = True
    return len(baris)


def cek_pinjaman(path, kata_kunci, nama_tipe, masalah):
    """`pinjaman.def`: asal unik, tidak boleh menabrak kata kunci Basa Jawa,
    dan setiap padanan harus benar-benar ada di Basa Jawa."""
    with open(path, encoding='utf-8') as fh:
        isi = fh.read()
    entri = []
    for no, baris in enumerate(isi.split('\n'), 1):
        if not baris.strip() or baris.lstrip().startswith('//'):
            continue
        m = re.match(r'\s*JAWA_PINJAMAN\("([^"]+)",\s*(Kunci|Bawaan),\s*"([^"]+)"\)', baris)
        if not m:
            masalah.append(f'{path}:{no}: baris tidak bisa dibaca: {baris.strip()[:60]}')
            continue
        entri.append((no, m.group(1), m.group(2), m.group(3)))

    dilihat = set()
    for no, asal, jenis, padanan in entri:
        if asal in dilihat:
            masalah.append(f'{path}:{no}: `asal` duplikat: {asal}')
        dilihat.add(asal)
        if asal in kata_kunci:
            masalah.append(
                f'{path}:{no}: `{asal}` sudah jadi kata kunci Basa Jawa -- entri mati, '
                f'lexer tidak akan pernah melihatnya sebagai pengenal')
        if jenis not in ('Kunci', 'Bawaan'):
            masalah.append(f'{path}:{no}: jenis tak dikenal: {jenis}')
        # Padanan boleh berupa kata kunci, frasa kata kunci, atau fungsi global.
        sah = (padanan in kata_kunci or padanan in FUNGSI_GLOBAL
               or padanan in nama_tipe
               or all(kata in kata_kunci for kata in padanan.split()))
        if not sah:
            masalah.append(
                f'{path}:{no}: padanan `{padanan}` tidak ada di Basa Jawa '
                f'(entri untuk `{asal}`)')
    return len(entri)


def cek_pesan(path, masalah):
    """`messages.def`: kode unik, awalan kode cocok dengan DiagKind, dan
    placeholder `{n}` muncul di kedua bahasa (kalau hanya di satu, output
    dwibahasa jadi tidak konsisten)."""
    with open(path, encoding='utf-8') as fh:
        isi = fh.read()
    # Baris komentar boleh memuat contoh `JAWA_MSG(...)`; jangan dihitung.
    kode_nyata = [ln for ln in isi.split('\n') if not ln.lstrip().startswith('//')]
    isi = '\n'.join(kode_nyata)
    n = 0
    dilihat = {}
    pola = re.compile(
        r'JAWA_MSG\("([A-Z][0-9]{3})",\s*DiagKind::(\w+),\s*'
        r'"((?:[^"\\]|\\.)*)",\s*"((?:[^"\\]|\\.)*)"', re.S)
    for m in pola.finditer(isi):
        n += 1
        kode, kind, jawa, indo = m.group(1), m.group(2), m.group(3), m.group(4)
        if kode in dilihat:
            masalah.append(f'{path}: kode duplikat {kode} (baris {dilihat[kode]} & {n})')
        dilihat[kode] = n
        awalan = kode[0]
        if awalan not in JENIS_DARI_AWALAN:
            masalah.append(f'{path}: kode {kode} punya awalan tak dikenal')
        elif kind not in JENIS_DARI_AWALAN[awalan]:
            masalah.append(
                f'{path}: kode {kode} berawalan `{awalan}` tapi DiagKind::{kind} '
                f'(harusnya salah satu dari {sorted(JENIS_DARI_AWALAN[awalan])})')
        ph_jawa = set(re.findall(r'\{(\d+)\}', jawa))
        ph_indo = set(re.findall(r'\{(\d+)\}', indo))
        if ph_jawa != ph_indo:
            masalah.append(
                f'{path}: {kode} placeholder tidak sama: Jawa {sorted(ph_jawa)} '
                f'vs Indonesia {sorted(ph_indo)}')
    # Hitung juga yang TIDAK cocok pola, supaya kode baru yang salah format
    # tidak lolos diam-diam.
    total = len(re.findall(r'JAWA_MSG\(', isi))
    if total != n:
        masalah.append(
            f'{path}: {total} JAWA_MSG tapi hanya {n} yang cocok pola '
            f'(baris lain tidak terbaca)')
    return n


def main() -> int:
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    masalah = []
    kata_kunci = baca_java_kunci(os.path.join(root, 'src/lex/keywords.def'))
    n_kw = cek_keyword(os.path.join(root, 'src/lex/keywords.def'), masalah)
    nama_tipe = baca_nama_tipe(os.path.join(root, 'src/rt/string.cpp'))
    n_pinj = cek_pinjaman(os.path.join(root, 'src/lex/pinjaman.def'),
                         kata_kunci, nama_tipe, masalah)
    n_pes = cek_pesan(os.path.join(root, 'src/support/messages.def'), masalah)

    if masalah:
        print(f'{len(masalah)} masalah pada tabel data:\n')
        for m in masalah:
            print(f'  {m}')
        return 1
    print(f'OK: {n_kw} baris kata kunci ({len(kata_kunci)} ejaan), '
          f'{n_pinj} entri pinjaman, {n_pes} pesan diagnostik -- semua invarian terpenuhi.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
