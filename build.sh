#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

# Prefer the installation's own flags (Intel/ARM Homebrew, system SDL, or a
# custom prefix). Arrays retain argument boundaries without shell eval.
sdl_flags=(-lSDL2)
if command -v pkg-config >/dev/null && pkg-config --exists sdl2; then
  read -r -a sdl_flags <<< "$(pkg-config --cflags --libs sdl2)"
elif command -v sdl2-config >/dev/null; then
  read -r -a sdl_flags <<< "$(sdl2-config --cflags --libs)"
elif [[ "$(uname)" == "Darwin" ]] && command -v brew >/dev/null; then
  sdl_prefix=$(brew --prefix sdl2)
  sdl_flags=(-I"$sdl_prefix/include" -L"$sdl_prefix/lib" -lSDL2)
fi

if [[ "$(uname)" == "Darwin" ]]; then
  "${CC:-clang}" -Os dilation.c -o dilation "${sdl_flags[@]}" -framework OpenGL -lm
else
  "${CC:-cc}" -Os dilation.c -o dilation "${sdl_flags[@]}" -lGL -lm
fi

echo "[build] dilation ready ($(ls -lh dilation | awk '{print $5}'))"
