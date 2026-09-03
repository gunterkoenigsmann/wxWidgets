#!/bin/bash
#
# Build every step of the series make-upstream-series.py produces.
#
# The series is cumulative and the build system comes last, so until step 17
# the toolkit is upstream's GTK+ 3 and the new files are not compiled at all.
# What each earlier step has to answer is therefore narrow but not free: do
# the *existing* files it changes still compile where they are, with only the
# headers the steps before it brought along? A file landing in a later group
# than the code calling it compiles fine at the tip and not at the step, which
# is what this catches -- window.cpp calling wxGtkScrollbarGetAdjustment() out
# of include/wx/gtk/private.h did exactly that.
#
# Each step is built incrementally on the one before, which is how the whole
# series fits in one pass rather than seventeen clean builds. A clean build of
# the first and last step is worth doing separately.
#
# Usage: build/tools/build-upstream-series.sh <worktree> <build dir>
#
# The worktree needs its submodules: the build stops on catch2 and on nanosvg
# otherwise, and neither has anything to do with the series.

set -u

W=${1:?usage: $0 <worktree> <build dir>}
B=${2:?usage: $0 <worktree> <build dir>}
: "${CONFIGURE_ARGS:=--with-gtk=3 --disable-shared --without-opengl}"
CONFIGURE_ARGS="$CONFIGURE_ARGS --disable-stc"

steps=$(cd "$W" && git branch --list 'upstream-series/*' \
                      | tr -d ' *' | sort)
if [ -z "$steps" ]; then
    echo "no upstream-series/* branches in $W;"
    echo "run make-upstream-series.py first"
    exit 1
fi

mkdir -p "$B" || exit 1
fail=0
first=1

for s in $steps; do
    name=${s#upstream-series/}

    if ! ( cd "$W" && git checkout -q "$s" ); then
        echo "$name CHECKOUT FAILED"
        fail=1
        continue
    fi

    # Only the first step and the one that changes which files are compiled
    # need configure run again -- and that one starts from nothing, because
    # the object lists it regenerates no longer match what is in the
    # directory.
    if [ "$name" = "17-build" ]; then
        rm -rf "${B:?}"/*
        first=1
    fi
    if [ $first = 1 ]; then
        if ! ( cd "$B" && "$W/configure" $CONFIGURE_ARGS \
                              > "configure.$name.log" 2>&1 ); then
            echo "$name CONFIGURE FAILED, see $B/configure.$name.log"
            fail=1
            continue
        fi
        first=0
    fi

    if ( cd "$B" && make -j"$(nproc)" > "build.$name.log" 2>&1 ); then
        echo "$name ok"
    else
        echo "$name FAILED: $(cd "$B" && grep -m1 'error:' "build.$name.log")"
        fail=1
    fi
done

exit $fail
