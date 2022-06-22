#!/usr/bin/env bash

set -eu

info() {
    echo -e "\033[33m$1\033[m"
}

die() {
    echo -e "\033[31m$1\033[m"
    exit 1
}

build_type="Release"
do_install="NO"

if test $# -ge 1; then
    case "$1" in
        "debug")
            build_type="Debug"
            ;;
        "clean")
            mkdir -p ./out
            rm -rf ./out clopts
            exit 0
            ;;
        "install")
            do_install="YES"
            ;;
        *)
            die "Unrecognised option '$1'"
            ;;
    esac
fi

mkdir -p ./out
cd out || die "cd error"
cmake -DCMAKE_BUILD_TYPE="$build_type" .. -GNinja
if test "$do_install" = "YES"; then ninja && sudo ninja install; else ninja; fi
