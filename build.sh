#!/bin/sh
# Builds labitop on Linux.
#   sh build.sh         build build/labitop
#   sh build.sh test    also build and run the tests
# Set CXX to use another compiler, for example: CXX=clang++ sh build.sh
set -e
cd "$(dirname "$0")"

CXX="${CXX:-g++}"
if ! command -v "$CXX" >/dev/null 2>&1; then
    echo "$CXX not found. Install a C++17 compiler first, for example: sudo apt install g++" >&2
    exit 1
fi

SRC="src/term.cpp src/parse.cpp src/nvml.cpp src/sys_win.cpp src/sys_linux.cpp"
FLAGS="-std=c++17 -O2 -Wall -Wextra"

mkdir -p build

echo "Building build/labitop ..."
$CXX $FLAGS src/main.cpp $SRC -o build/labitop -ldl

if [ "$1" = "test" ]; then
    echo "Building and running tests ..."
    $CXX $FLAGS tests/test.cpp $SRC -o build/labitop_test -ldl
    ./build/labitop_test
fi

echo "Done: build/labitop"
