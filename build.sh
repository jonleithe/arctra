#!/usr/bin/env bash

set -euo pipefail

readonly project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

build_preset() {
    local preset="$1"
    cmake --preset "$preset" -S "$project_dir"
    cmake --build --preset "$preset"
}

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 {all|debug|release}" >&2
    exit 2
fi

case "$1" in
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
    *)
        echo "Unknown build type: $1" >&2
        echo "Usage: $0 {all|debug|release}" >&2
        exit 2
        ;;
esac
