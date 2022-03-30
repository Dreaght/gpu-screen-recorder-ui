#!/bin/sh

[ $# -ne 1 ] && echo "usage: build.sh debug|release" && exit 1

script_dir=$(dirname "$0")
cd "$script_dir"

sibs build --"$1" gpu-screen-recorder-overlay-daemon
sibs build --"$1"
echo "Successfully built"
