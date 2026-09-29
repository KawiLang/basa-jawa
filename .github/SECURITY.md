# Keamanan

## Melaporkan kerentanan

**Jangan buka issue publik untuk kerentanan.**

Kalau `jawa` bisa dieksekusi kode asing, crash, atau membaca data di luar
wilayah program, itu masalah keamanan - bukan bug biasa. Laporkan secara pribadi
ke maintainer dengan cara:

- Bukat [security advisory](https://github.com/KawiLang/basa-jawa/security/advisories/new)
  di repo ini (privat sampai dipublikasikan), atau
- kirim pesan ke maintainer repo.

Sertakan: versi `jawa versi`, OS dan compiler, program reproduksi sekecil
mungkin, dan dampak yang kamu amati.

Laporan yang sudah dikonfirmasi akan kami tangani lewat advisory, lalu
diberitahukan. Laporan yang dilakukan dengan itikad baik dan disertai detail
kami hargai.

## Ruang lingkup

| Masalah | Dianggap kerentanan |
|---|---|
| `jawa run` pada berkas `.jw` yang tidak dipercaya bisa menjalankan perintah sistem | ya |
| Galat runtime yang bisa dipicu dari input sampai menyebabkan crash atau UB | ya |
| Kebocoran memori, use-after-free, data race yang bisa dipicu program | ya |
| Query regex yang bisa membuat proses hang (mis. lewat anggaran langkah) | ya, bila lolos dari batas yang ada |
| Luapan stack/heap pada bytecode yang crafted | ya |
| Bug yang hanya bisa dipicu dari build di luar dukungan atau opsi eksperimental | biasanya tidak |

## Batasan yang sudah ada

Proyek ini sudah punya beberapa mitigasi. Tapi mitigasi bukan pengganti laporan.

- Anggaran langkah regex dan batas instruksi VM (`--maks-langkah`) mencegah
  konsumsi waktu tak terbatas.
- `--maks-memori` dan `--maks-tumpukan` membatasi pemakaian sumber daya.
- `--gc-stress` dipakai untuk mengejar bug akar GC.

Semua batas tersebut bisa dimatikan atau dilewati oleh input yang sengaja
dirancang. Kalau kamu menemukan program yang melewati batas itu, tolong laporkan.
