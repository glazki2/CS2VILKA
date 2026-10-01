#!/usr/bin/env bash
set -euo pipefail

readonly BUILD_DIR="${1:-build-linux-release}"
readonly SOURCE_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly IMAGE="registry.gitlab.steamos.cloud/steamrt/sniper/sdk@sha256:2969e5a47146a6494c01d953cd818b1d62712f42f9e54c4809d7a3aa8dc276ce"

[[ "$BUILD_DIR" =~ ^[A-Za-z0-9_.-]+$ ]] ||
  { echo "The build folder name may only contain letters, numbers, dots, underscores, and dashes." >&2; exit 1; }
command -v docker >/dev/null ||
  { echo "Docker is required for the Linux Steam Runtime build." >&2; exit 1; }

"$SOURCE_DIR/bootstrap.sh"
docker version --format '{{.Server.Version}}' >/dev/null

docker run --rm \
  -v "$SOURCE_DIR:/workspace" \
  -v "$SOURCE_DIR/.tools/ambuild:/opt/ambuild:ro" \
  -w /workspace \
  "$IMAGE" bash -lc "
    set -eu
    export DEBIAN_FRONTEND=noninteractive PYTHONPATH=/opt/ambuild
    apt-get update >/dev/null
    apt-get install -y --no-install-recommends lld >/dev/null
    test -d '$BUILD_DIR/.ambuild2' || python3 configure.py --enable-optimize --out '$BUILD_DIR'
    python3 -c 'from ambuild2.run import cli_run; cli_run()' '$BUILD_DIR'
  "

plugin="$SOURCE_DIR/$BUILD_DIR/package/game/csgo/addons/csvilka/bin/linuxsteamrt64/csvilka.so"
[[ -f "$plugin" ]] || { echo "The build finished without producing csvilka.so." >&2; exit 1; }
echo "Linux package ready: $plugin"
