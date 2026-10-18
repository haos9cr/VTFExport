#!/usr/bin/env bash
# Runs the test suite on Linux. No Photoshop SDK needed.
#
#   tests/run_tests.sh            core tests (g++ + python3 with srctools)
#   tests/run_tests.sh --wine     + plug-in tests: cross-compiles VTFExport.8bi with
#                                   MinGW against tests/sdk_stub and runs fake
#                                   Photoshop hosts under Wine
#
# Requirements: g++, python3, `pip install srctools`;
# for --wine also x86_64-w64-mingw32-g++ / -windres and wine.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/tests"
WINE_TESTS=0
[[ "${1:-}" == "--wine" ]] && WINE_TESTS=1
rm -rf "$OUT"
mkdir -p "$OUT"
cd "$OUT"

CXX="${CXX:-g++}"
CXXFLAGS="-std=c++17 -O2 -I$ROOT/src -w"
CORE="$ROOT/src/VtfWriter.cpp $ROOT/src/VtfReader.cpp $ROOT/src/Resample.cpp"
PY="python3 -I"
fail=0
step() { echo; echo "=== $* ==="; }
run() { if ! "$@"; then echo "*** FAILED: $*"; fail=1; fi; }

step "Writer: every format x versions 7.1-7.5 (checked with srctools)"
$CXX $CXXFLAGS "$ROOT/tests/test_writer.cpp" $CORE -o test_writer
mkdir -p writer
run ./test_writer writer
run $PY "$ROOT/tests/validate.py" writer | tail -3

step "Reader vs srctools (formats, animation, cubemaps, volume, broken files)"
$CXX $CXXFLAGS "$ROOT/tests/read_dump.cpp" $CORE -o read_dump
run $PY "$ROOT/tests/validate_reader.py" ./read_dump writer reader | tail -12

step "Filters and resize"
$CXX $CXXFLAGS "$ROOT/tests/resample_test.cpp" $CORE -o resample_test
run ./resample_test

step "Animation write/read"
$CXX $CXXFLAGS "$ROOT/tests/anim_test.cpp" $CORE -o anim_test
mkdir -p anim
run ./anim_test anim
run $PY "$ROOT/tests/validate_anim.py" anim

step "Language files"
run $PY "$ROOT/tools/check_lang.py"

if [[ $WINE_TESTS == 1 ]]; then
    MCXX=x86_64-w64-mingw32-g++
    STUB="$ROOT/tests/sdk_stub"
    export WINEDEBUG=-all WINEPREFIX="$OUT/wineprefix"

    step "Cross-compiling VTFExport.8bi against the SDK stub"
    $PY "$ROOT/tools/embed_lang.py" "$ROOT/lang" gen/lang_embedded.inc
    sed '/VTFExport.pipl/d' "$ROOT/src/VTFExport.rc" > res.rc
    x86_64-w64-mingw32-windres -c 65001 -I "$ROOT/src" res.rc -O coff -o res.o
    $MCXX -std=c++17 -O2 -w -municode -DUNICODE -D_UNICODE -DNOMINMAX -D_WIN32_WINNT=0x0601 -I "$ROOT/src" -I "$STUB" -I gen -shared \
        "$ROOT"/src/VTFExport.cpp "$ROOT"/src/VTFExportUI.cpp "$ROOT"/src/Lang.cpp $CORE res.o \
        -o VTFExport.8bi -static -luser32 -ladvapi32 -lshell32
    for t in fake_host fake_host_read; do
        $MCXX -std=c++17 -O2 -w -I "$STUB" "$ROOT/tests/$t.cpp" -o $t.exe -static -ladvapi32
    done
    $MCXX -std=c++17 -O2 -w -I "$STUB" -I "$ROOT/src" "$ROOT/tests/fake_host_layers.cpp" $CORE \
        -o fake_host_layers.exe -static -ladvapi32
    $MCXX -std=c++17 -O2 -w -municode -I "$ROOT/src" -I gen "$ROOT/tests/lang_test.cpp" "$ROOT/src/Lang.cpp" \
        -o lang_test.exe -static
    wpath() { echo "Z:$(realpath "$1")" | sed 's#/#\\#g'; }

    step "Fake host: save (8/16/32 bit, RGB/Gray, alpha) - BGRA8888 and Auto"
    for fmt in 12 1000; do
        mkdir -p host$fmt
        run wine fake_host.exe VTFExport.8bi "$(wpath host$fmt)" $fmt
        run $PY "$ROOT/tests/validate_host.py" host$fmt | tail -1
    done

    step "Fake host: open + re-save keeps settings"
    mkdir -p roundtrip
    srcs=(writer/DXT5_75.vtf writer/DXT1_72.vtf writer/BGRA5551_71.vtf writer/I8_73.vtf writer/IA88_74.vtf
          writer/BGRA8888_72.vtf reader/st_ABGR8888.vtf reader/st_RGB888_BLUESCREEN.vtf)
    for f in "${srcs[@]}"; do
        b=$(basename "$f" .vtf)
        run wine fake_host_read.exe VTFExport.8bi "$(wpath "$f")" "$(wpath roundtrip)\\$b.rgba" \
            "$(wpath roundtrip)\\$b.resaved.vtf" > /dev/null
    done
    run $PY "$ROOT/tests/validate_roundtrip.py" roundtrip "${srcs[@]}" | tail -1

    step "Fake host: animation as layers (order, names, hidden layer, merged image)"
    mkdir -p layers
    run wine fake_host_layers.exe VTFExport.8bi "$(wpath anim/anim_bgra_75.vtf)" "$(wpath layers)"

    step "Languages: embedded + folder, key-by-key override"
    mkdir -p langdir
    printf 'language.name = Русский (fix)\nlanguage.code = ru\ndlg.title = Override\n' > langdir/ru_fix.lang
    run wine lang_test.exe "$(wpath langdir)"
fi

echo
if [[ $fail == 0 ]]; then echo "ALL TESTS PASSED"; else echo "SOME TESTS FAILED"; fi
exit $fail
