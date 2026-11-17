#!/bin/sh
# Installs labitop, so that "labitop" works in any terminal.
#   sh install.sh                               build, copy to ~/.local/bin
#   sh build.sh && sudo PREFIX=/usr/local sh install.sh   install for all users
#   sh install.sh uninstall                     remove it again (use the same PREFIX)
set -e
cd "$(dirname "$0")"

PREFIX="${PREFIX:-$HOME/.local}"
BIN="$PREFIX/bin"

if [ "$1" = "uninstall" ]; then
    rm -f "$BIN/labitop"
    echo "Removed: $BIN/labitop"
    exit 0
fi

# Build as the normal user; under sudo, reuse that build instead of making root-owned files.
if [ "$(id -u)" -ne 0 ] || [ ! -x build/labitop ]; then
    sh build.sh
fi

mkdir -p "$BIN"
install -m 755 build/labitop "$BIN/labitop"
echo "Installed: $BIN/labitop"

case ":$PATH:" in
    *":$BIN:"*)
        echo "Run: labitop"
        ;;
    *)
        echo "$BIN is not in PATH yet. Add this line to ~/.bashrc (or ~/.zshrc), then open a new terminal:"
        echo "    export PATH=\"$BIN:\$PATH\""
        ;;
esac
