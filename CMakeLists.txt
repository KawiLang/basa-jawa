cmake_minimum_required(VERSION 3.24)
project(jawa
    VERSION 1.0.0
    DESCRIPTION "Basa Jawa — bahasa pemrograman sintaks-JavaScript berkata Jawa"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Versi & bahasa
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Tipe build" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Hierarki: semua kode di src/ tahu header internal lewat include path target.
# Opsi: JAWA_NAN_BOXING=OFF memakai Value 16-byte.

# ---------------------------------------------------------------------------
# Opsi
# ---------------------------------------------------------------------------
option(JAWA_BUILD_TESTS      "Bangun test"            ON)
option(JAWA_BUILD_FUZZ       "Bangun target fuzzing"  OFF)
option(JAWA_BUILD_EXAMPLES   "Bangun contoh embedding" ON)
option(JAWA_NAN_BOXING      "Value 8-byte NaN-boxing" ON)
option(JAWA_WERROR          "Perlakukan warning sebagai galat" ON)

set(JAWA_SANITIZER "none" CACHE STRING "Sanitizer: none|address|undefined|thread")
option(JAWA_COVERAGE "Instrumentasi gcov" OFF)

# ---------------------------------------------------------------------------
# Target inti: libjawa
# ---------------------------------------------------------------------------
set(JAWA_SOURCES
    src/support/support.cpp
    src/rt/number.cpp
    src/rt/object.cpp
    src/rt/string.cpp
    src/rt/regexp.cpp
    src/rt/tanggal.cpp
    src/gc/heap.cpp
    src/vm/chunk.cpp
    src/vm/vm.cpp
    src/vm/vm_exec.cpp
    src/vm/vm_loop.cpp
    src/vm/vm_prop.cpp
    src/vm/vm_async.cpp
    src/vm/linker.cpp
    src/vm/vm_gen.cpp
    src/compile/compiler.cpp
    src/compile/compiler_stmt.cpp
    src/compile/compiler_expr.cpp
    src/stdlib/stdlib.cpp
    src/stdlib/stdlib_peta.cpp
    src/lex/lexer.cpp
    src/parse/parser.cpp
    src/parse/parser_class.cpp
    src/parse/parser_expr.cpp
    src/parse/parser_literal.cpp
    src/parse/parser_match.cpp
    src/parse/ast_print.cpp
    src/cli/main.cpp
    src/cli/tes.cpp
    src/cli/fmt.cpp
    src/cli/repl.cpp
)

add_library(jawa STATIC ${JAWA_SOURCES})

add_library(jawa::jawa ALIAS jawa)

target_include_directories(jawa
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        # Hanya `src/` — header dalam satu direktori memakai include relatif
        # (aturan include bertanda petik), sehingga kita TIDAK menambahkan
        # `src/rt` dsb. Alasannya: `src/rt/string.h` akan menimpa header sistem
        # `<string.h>` bila `src/rt` ada di jalur include.
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(jawa PUBLIC cxx_std_20)

# --- warning ketat (wajib) ---
if(MSVC)
    target_compile_options(jawa PRIVATE /W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc)
    if(JAWA_WERROR)
        target_compile_options(jawa PRIVATE /WX)
    endif()
    target_compile_definitions(jawa PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(jawa PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow
        -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
        -Wunused -Woverloaded-virtual -Wdouble-promotion
        -Wformat=2 -Wimplicit-fallthrough
    )
    if(JAWA_WERROR)
        target_compile_options(jawa PRIVATE -Werror)
    endif()
endif()

# --- NaN-boxing ---
# PENTING: definisi harus PUBLIC, bukan PRIVATE. `rt/value.h` memilih definisi
# kelas `Value` berdasarkan `JAWA_NO_NAN_BOX`; kalau hanya pustaka yang
# menerimanya, setiap target yang meng-include header itu (CLI, unit test)
# melihat versi 8-byte sementara pustaka memakai 16-byte -- pelanggaran ODR
# yang gejalanya jauh dari lokasi penyebabnya.
if(JAWA_NAN_BOXING)
    target_compile_definitions(jawa PUBLIC JAWA_NAN_BOX=1)
else()
    target_compile_definitions(jawa PUBLIC JAWA_NO_NAN_BOX=1)
endif()

# --- sanitizer ---
if(NOT JAWA_SANITIZER STREQUAL "none")
    message(STATUS "Sanitizer aktif: ${JAWA_SANITIZER}")
    if(MSVC)
        if(JAWA_SANITIZER STREQUAL "address")
            target_compile_options(jawa PUBLIC /fsanitize=address)
        endif()
    else()
        set(_jawa_san "-fsanitize=${JAWA_SANITIZER}")
        set(_jawa_san_flags "-fno-omit-frame-pointer" "-g")
        target_compile_options(jawa PUBLIC "${_jawa_san}" ${_jawa_san_flags})
        target_link_options(jawa PUBLIC "${_jawa_san}" ${_jawa_san_flags})
    endif()
endif()

if(JAWA_COVERAGE AND NOT MSVC)
    target_compile_options(jawa PUBLIC --coverage -O0 -g)
    target_link_options(jawa PUBLIC --coverage)
endif()

# --- platform libs ---
if(NOT MSVC)
    find_package(Threads REQUIRED)
    target_link_libraries(jawa PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
endif()

# ---------------------------------------------------------------------------
# CLI jawa
# ---------------------------------------------------------------------------
add_executable(jawa_cli src/cli/jawa_main.cpp)
set_target_properties(jawa_cli PROPERTIES OUTPUT_NAME jawa)
target_include_directories(jawa_cli PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(jawa_cli PRIVATE jawa)

# ---------------------------------------------------------------------------
# Test (harness sendiri, tanpa dependensi eksternal)
# ---------------------------------------------------------------------------
if(JAWA_BUILD_TESTS)
    enable_testing()
    add_library(jawa_test STATIC tests/harness.cpp)
    target_include_directories(jawa_test PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(jawa_test PUBLIC jawa)

    file(GLOB JAWA_TEST_FILES
         ${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/*.cpp
         ${CMAKE_CURRENT_SOURCE_DIR}/tests/gc/*.cpp
         ${CMAKE_CURRENT_SOURCE_DIR}/tests/native/*.cpp)
    foreach(tf ${JAWA_TEST_FILES})
        get_filename_component(tname ${tf} NAME_WE)
        add_executable(${tname} ${tf})
        target_link_libraries(${tname} PRIVATE jawa_test)
        add_test(NAME unit.${tname} COMMAND ${tname})
        set_tests_properties(unit.${tname} PROPERTIES TIMEOUT 300)
    endforeach()

    # Uji emas: bandingkan `jawa run examples/*.jw` dengan tests/golden/*.out.
    # (Butuh python3; dilewati diam-diam kalau tidak ada.)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME golden.contoh
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/cek_golden.py)
        set_tests_properties(golden.contoh PROPERTIES TIMEOUT 300)

        # Uji emas dengan --gc-stress: memicu koleksi SETIAP alokasi. Lambat,
        # tapi menangkap bug akar yang hilang.
        add_test(NAME golden.contoh_gcstress
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/cek_golden.py --gc-stress)
        set_tests_properties(golden.contoh_gcstress PROPERTIES TIMEOUT 1800)

        # Verifikasi front-end: semua contoh acuan harus ter-parse bersih.
        add_test(NAME golden.cek_contoh
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/cek_contoh.py)
        set_tests_properties(golden.cek_contoh PROPERTIES TIMEOUT 300)

        # `jawa repl` interaktif: dijalankan dengan masukan terkontrol.
        add_test(NAME golden.cek_repl
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/cek_repl.py)
        set_tests_properties(golden.cek_repl PROPERTIES TIMEOUT 600)

        # `jawa fmt` harus idempoten dan tidak mengubah perilaku. Idempoten
        # adalah syarat `--tulis` berguna: kalau tidak, setiap commit
        # menghasilkan diff baru.
        add_test(NAME golden.cek_fmt
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/cek_fmt.py)
        set_tests_properties(golden.cek_fmt PROPERTIES TIMEOUT 600)
    else()
        message(STATUS "python3 tidak ditemukan: uji emas dilewati")
    endif()

    # Uji bahasa Basa Jawa (`jawa tes`). Berkas uji memakai sub-perintah `tes`,
    # jadi target `jawa_cli` harus sudah dibangun -- `add_test` di bawah
    # dievaluasi saat konfigurasi, sementara `jawa_cli` didefinisikan di atas,
    # jadi urutannya aman.
    add_test(NAME tes.bahasa
             COMMAND jawa_cli tes ${CMAKE_CURRENT_SOURCE_DIR}/tests/tes)
    set_tests_properties(tes.bahasa PROPERTIES TIMEOUT 300)

    # Uji bahasa dengan `--gc-stress`: koleksi tiap alokasi. Lambat, tapi
    # menangkap bug akar GC yang hilang tanpa mode ini.
    add_test(NAME tes.bahasa_gcstress
             COMMAND jawa_cli --gc-stress tes ${CMAKE_CURRENT_SOURCE_DIR}/tests/tes)
    set_tests_properties(tes.bahasa_gcstress PROPERTIES TIMEOUT 1800)
endif()

# ---------------------------------------------------------------------------
# Target fuzzing
# ---------------------------------------------------------------------------
# 5 target, masing-masing dengan dua bentuk:
#   * GCC 12 (toolchain proyek): driver mandiri deterministik -- PRNG splitmix64,
#     korpus seed + mutasi. Tidak perlu libFuzzer.
#   * clang + libFuzzer: `-DJAWAFUZZ_LIBFUZZER` mendefinisikan
#     `LLVMFuzzerTestOneInput`, jadi berkas yang sama langsung bisa dipakai
#     `clang -fsanitize=fuzzer`. Pilih lewat `JAWA_FUZZ_LIBFUZZER`.
if(JAWA_BUILD_FUZZ)
    add_library(jawa_fuzz_support INTERFACE)
    target_include_directories(jawa_fuzz_support INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz
        # Sama seperti `jawa_test`: header dalam satu direktori memakai include
        # relatif, jadi yang perlu ditambahkan hanya akar `src/`.
        ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(jawa_fuzz_support INTERFACE jawa)
    target_compile_features(jawa_fuzz_support INTERFACE cxx_std_20)

    file(GLOB JAWA_FUZZ_FILES ${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/fuzz_*.cpp)
    list(LENGTH JAWA_FUZZ_FILES JAWA_FUZZ_COUNT)
    if(JAWA_FUZZ_COUNT LESS 5)
        message(FATAL_ERROR "spesifikasi meminta 6 target fuzz; ditemukan ${JAWA_FUZZ_COUNT}")
    endif()
    foreach(ff ${JAWA_FUZZ_FILES})
        get_filename_component(fname ${ff} NAME_WE)
        add_executable(${fname} ${ff})
        target_link_libraries(${fname} PRIVATE jawa_fuzz_support)
        if(NOT MSVC)
            target_compile_options(${fname} PRIVATE -Wall -Wextra -Wshadow -Wold-style-cast)
            if(JAWA_WERROR)
                target_compile_options(${fname} PRIVATE -Werror)
            endif()
        endif()
        if(JAWA_FUZZ_LIBFUZZER)
            target_compile_definitions(${fname} PRIVATE JAWAFUZZ_LIBFUZZER)
            target_compile_options(${fname} PRIVATE -fsanitize=fuzzer)
        endif()
    endforeach()

    # Driver mandiri butuh POSIX (`dup2` untuk menyendapkan stderr) dan tidak
    # ada di Windows. Batasnya kecil (400 kasus) supaya muat di ctest; campaign
    # serius pakai `scripts/fuzz_jalankan.py` langsung.
    if(NOT WIN32 AND NOT JAWAFUZZ_LIBFUZZER AND TARGET fuzz_vm)
        find_package(Python3 COMPONENTS Interpreter)
        if(Python3_Interpreter_FOUND)
            # Preset `fuzz` mematikan `JAWA_BUILD_TESTS`, jadi `enable_testing()`
            # belum dipanggil di titik ini.
            enable_testing()
            foreach(fname fuzz_lexer fuzz_parser fuzz_kompilasi fuzz_vm fuzz_modul fuzz_regex)
                add_test(NAME fuzz.${fname}
                         COMMAND ${Python3_EXECUTABLE}
                                 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/fuzz_jalankan.py
                                 --satuan --batas 400
                                 --target ${fname}
                                 --build $<TARGET_FILE_DIR:fuzz_vm>)
                set_tests_properties(fuzz.${fname} PROPERTIES TIMEOUT 900)
            endforeach()
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# Instalasi
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS jawa jawa_cli
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
# CLI `jawa` memakai simbol `main` dari libjawa, jadi tidak perlu target terpisah.
