#!/usr/bin/env bash
set -euo pipefail

configuration="${1:-debug}"
case "$configuration" in
  debug) preset=dev ;;
  release) preset=release ;;
  *) echo "usage: $0 [debug|release]" >&2; exit 2 ;;
esac

command -v node >/dev/null || { echo "Node.js is required to build the Cesium frontend." >&2; exit 1; }
command -v npm >/dev/null || { echo "npm is required to build the Cesium frontend." >&2; exit 1; }

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root/globe"
npm ci
npm run build
cd "$root"
cmake --build --preset "$preset" --target arctra arctra-globe-worker
echo "Run: ./build/$preset/arctra globe"
