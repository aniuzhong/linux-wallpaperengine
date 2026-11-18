#!/bin/bash
# Format C/C++ sources with the same clang-format version used by CI.
# CI uses jidicula/clang-format-action@v4.18.0, which defaults to clang-format 22.
set -euo pipefail

CLANG_FORMAT_MAJOR="${CLANG_FORMAT_MAJOR:-22}"
CLANG_FORMAT_BIN="${CLANG_FORMAT_BIN:-}"

if [[ -z $CLANG_FORMAT_BIN ]]; then
    if command -v "clang-format-${CLANG_FORMAT_MAJOR}" >/dev/null 2>&1; then
        CLANG_FORMAT_BIN="clang-format-${CLANG_FORMAT_MAJOR}"
    elif command -v clang-format >/dev/null 2>&1; then
        CLANG_FORMAT_BIN=clang-format
    else
        echo "error: clang-format-${CLANG_FORMAT_MAJOR} not found" >&2
        echo "install it with: sudo apt-get install clang-format-${CLANG_FORMAT_MAJOR}" >&2
        exit 1
    fi
fi

found_major="$("$CLANG_FORMAT_BIN" --version | grep -oE 'version [0-9]+' | awk '{print $2}')"
if [[ $found_major != "$CLANG_FORMAT_MAJOR" ]]; then
    echo "error: $CLANG_FORMAT_BIN is version ${found_major}, CI uses version ${CLANG_FORMAT_MAJOR}" >&2
    exit 1
fi

mapfile -t files < <(
    find src -name .git -prune -o \
        -regextype posix-egrep \
        -regex '^.*\.((((c|C)(c|pp|xx|\+\+)?$)|((h|H)h?(pp|xx|\+\+)?$))|(ino|pde|proto|cu))$' \
        -print | grep -v '^src/External'
)

echo "Using $("$CLANG_FORMAT_BIN" --version)"
"$CLANG_FORMAT_BIN" -i --style=file --fallback-style=llvm "${files[@]}"