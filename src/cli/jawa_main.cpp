// Entry point biner `jawa`.
//
// implementasi perintah ada di src/cli/main.cpp (fungsi jawa::cli::jalankan)
// supaya tidak ada dua definisi `main` ketika pustaka ditautkan ke test.
#include "cli/cli.h"

int main(int argc, char** argv) { return jawa::cli::jalankan(argc, argv); }
