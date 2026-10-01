#!/usr/bin/env bash
set -euo pipefail

readonly AMBUILD_COMMIT="d89ec91a7ac2607da07b50bb62346f9a10e9a998"
readonly SOURCE_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly TOOLS_DIR="$SOURCE_DIR/.tools"
readonly AMBUILD_DIR="$TOOLS_DIR/ambuild"

command -v git >/dev/null || { echo "Git is required. Install it and run this script again." >&2; exit 1; }
command -v python3 >/dev/null || { echo "Python 3 is required. Install it and run this script again." >&2; exit 1; }
python3 -c 'import sys; raise SystemExit(0 if sys.version_info >= (3, 8) else 1)' ||
  { echo "Python 3.8 or newer is required." >&2; exit 1; }

cd "$SOURCE_DIR"
if [[ -d .git ]]; then
  git submodule update --init --recursive
elif [[ ! -d hl2sdk-cs2/public || ! -d metamod-source/core ]]; then
  echo "This source copy is missing the CS2 SDK or Metamod dependency." >&2
  exit 1
fi

mkdir -p "$TOOLS_DIR"
if [[ ! -d "$AMBUILD_DIR/.git" && ! -d "$AMBUILD_DIR/ambuild2" ]]; then
  git clone --filter=blob:none https://github.com/alliedmodders/ambuild.git "$AMBUILD_DIR"
fi

if [[ -d "$AMBUILD_DIR/.git" ]]; then
  current_commit="$(git -C "$AMBUILD_DIR" rev-parse HEAD)"
  if [[ "$current_commit" != "$AMBUILD_COMMIT" ]]; then
    [[ -z "$(git -C "$AMBUILD_DIR" status --porcelain)" ]] ||
      { echo "The local .tools/ambuild folder has changes. Remove or save them before bootstrapping again." >&2; exit 1; }
    git -C "$AMBUILD_DIR" fetch --depth=1 origin "$AMBUILD_COMMIT"
    git -C "$AMBUILD_DIR" checkout --detach "$AMBUILD_COMMIT"
  fi
fi

echo "CSVILKA build dependencies are ready."
