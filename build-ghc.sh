#!/bin/bash

TOP="$(readlink -f $(dirname $0))"
bash_args="--login -c"
make_args="make all -w -C $TOP -f Makefile.ghc"

if [ -z "$MSYS32" ]; then MSYS32=/e/msys32; fi
if [ -z "$MSYS64" ]; then MSYS64=/e/msys64; fi

set -x

MSYSTEM=MING64 $MSYS64/usr/bin/bash.exe $bash_args "PATH=/mingw64/bin:\$PATH $make_args"
MSYSTEM=MING32 $MSYS32/usr/bin/bash.exe $bash_args "PATH=/mingw32/bin:\$PATH $make_args"

tar -C $TOP -cvzf ghc-jailbreak.tar.gz i686/ x86_64/
