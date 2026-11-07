#!/bin/sh
set -eu
folder=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ "$(uname -sm)" != "Linux x86_64" ]; then
    printf '%s\n' 'This benchmark launcher requires Linux x86-64.' >&2
    exit 1
fi
if [ ! -x "$folder/benchmark" ]; then
    chmod u+x "$folder/benchmark"
fi
exec "$folder/benchmark" "$@"
