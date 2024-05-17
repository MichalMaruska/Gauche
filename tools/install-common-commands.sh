#!/bin/sh

# Create a symlink (if possible) or copy of the commadns with common-name.
# Used to create common name commands when --enable-common-commands is given.

usage() {
    echo "install-common-commands.h <BINDIR> <DESTDIR>"
    exit 1
}

bindir=$1
destdir=$2                      # aux prefix when install for packaging

if [ -z "$bindir" ]; then usage; fi

target="$destdir/$bindir"

if uname -a | grep -i 'mingw'; then
    LN_S=cp
else
    LN_S='ln -s'
fi

makelink() {
    gauche_name=$1
    common_name=$2

    rm -f $target/$common_name
    if uname -a | grep -i 'mingw'; then
        cp "$target/$gauche_name" "$target/$common_name"
    else
        ln -s "$gauche_name" "$target/$common_name"
    fi
}


makelink gauche-compile-r7rs compile-r7rs