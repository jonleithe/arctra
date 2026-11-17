#!/usr/bin/env bash

set -euo pipefail

readonly project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

usage() {
    echo "Usage: $0 {all|debug|release} [--no-globe]" >&2
}

build_preset() {
    local preset="$1"
    cmake --preset "$preset" -S "$project_dir"
    cmake --build --preset "$preset"
}

build_globe() {
    command -v node >/dev/null || {
        echo "Node.js 20.19+ or 22.12+ is required to build the Cesium globe." >&2
        echo "Use --no-globe for a C++-only build." >&2
        exit 1
    }
    command -v npm >/dev/null || {
        echo "npm is required to build the Cesium globe." >&2
        echo "Use --no-globe for a C++-only build." >&2
        exit 1
    }
    node -e '
        const [major, minor] = process.versions.node.split(".").map(Number);
        if (!((major === 20 && minor >= 19) || (major === 22 && minor >= 12) || major > 22)) process.exit(1);
    ' || {
        echo "Node.js 20.19+ or 22.12+ is required; found $(node --version)." >&2
        echo "Use --no-globe for a C++-only build." >&2
        exit 1
    }

    echo "Building Cesium globe assets"
    npm --prefix "$project_dir/globe" ci
    npm --prefix "$project_dir/globe" run build
}

if [[ $# -lt 1 || $# -gt 2 ]]; then
    usage
    exit 2
fi

readonly configuration="$1"
case "$configuration" in
    all|debug|release) ;;
    *)
        echo "Unknown build type: $configuration" >&2
        usage
        exit 2
        ;;
esac

build_globe_assets=true
if [[ $# -eq 2 ]]; then
    if [[ "$2" != "--no-globe" ]]; then
        echo "Unknown option: $2" >&2
        usage
        exit 2
    fi
    build_globe_assets=false
fi

if [[ "$build_globe_assets" == true ]]; then
    build_globe
fi

case "$configuration" in
    all)
        build_preset dev
        build_preset release
        ;;
    debug)
        build_preset dev
        ;;
    release)
        build_preset release
        ;;
esac
