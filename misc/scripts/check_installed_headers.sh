#!/bin/sh
#
# Compile every public header against nothing but the installed tree.
#
# The build tree has all of include/wx in it, so a header that the build
# never installs still compiles there, and the omission only shows up in
# somebody else's project. That is how wx/generic/dirdlgg.h reached a user:
# wx/dirdlg.h includes it under __WXGTK4__, the CMake build did not install
# it, and nothing between the two noticed.
#
# So this asks the question an application asks: with only what "make
# install" put on the disk, and only the flags wx-config hands out, does
# every header in tests/allheaders.h still compile?
#
# Run it after installing. WX_CONFIG can name the wx-config to ask, and CXX
# the compiler to use.

set -e

wx_config=${WX_CONFIG:-wx-config}
srcdir=`dirname "$0"`/../..

if [ ! -x "$wx_config" ] && ! command -v "$wx_config" >/dev/null 2>&1; then
    echo "ERROR - $wx_config not found: is wxWidgets installed?" >&2
    exit 1
fi

cxxflags=`"$wx_config" --cxxflags`

# The include directory of the installation itself, as opposed to the one
# holding the generated setup.h, which is also in --cxxflags.
incdir=
for d in `echo "$cxxflags" | tr ' ' '\n' | sed -n 's/^-I//p'`; do
    if [ -f "$d/wx/wx.h" ]; then incdir=$d; break; fi
done

if [ -z "$incdir" ]; then
    echo "ERROR - no installed wx/wx.h found in: $cxxflags" >&2
    exit 1
fi

# Headers that are listed in allheaders.h but are deliberately not installed
# by every port, so their absence is not a fault:
#
#  - the two MSW AUI art providers are in AUI_MSW_HDR, added only for wxMSW;
#  - the GTK one is in AUI_GTK_HDR, which build/cmake/lib/aui/CMakeLists.txt
#    adds only for wxGTK2.
#
# Each is checked below, and if one of them starts being installed it is a
# sign that this list is out of date rather than something to ignore.
not_installed_ok='wx/aui/barartmsw.h wx/aui/tabartmsw.h wx/aui/tabartgtk.h'

tmpdir=`mktemp -d`
trap 'rm -rf "$tmpdir"' EXIT INT TERM

rc=0
skip=
for h in $not_installed_ok; do
    if [ -f "$incdir/$h" ]; then
        echo "ERROR - $h is installed here after all, so it should be taken"
        echo "        out of not_installed_ok in $0"
        rc=1
    else
        skip="$skip $h"
    fi
done

awk -v skip="$skip" '
BEGIN { n = split(skip, notHere, " ") }
{
    for (i = 1; i <= n; i++)
        if ($0 == "#include <" notHere[i] ">")
        {
            print "/* " $0 " -- not installed for this port */"
            next
        }
    print
}' "$srcdir/tests/allheaders.h" > "$tmpdir/allheaders.h"

cat > "$tmpdir/check.cpp" <<EOF
#include "wx/wxprec.h"
#include "allheaders.h"

int main() { return 0; }
EOF

if ! ${CXX:-c++} -fsyntax-only -I"$tmpdir" $cxxflags "$tmpdir/check.cpp"; then
    echo
    echo "=============================== ERROR ==============================="
    echo "A public header does not compile against the installed tree alone."
    echo "If the error above is a missing file, that header is reachable from"
    echo "a public one but is not installed: add it to this port's _HDR list"
    echo "in build/files, then regenerate the generated lists with"
    echo "misc/scripts/check_files_lists.sh."
    # Supplementary, and only when it has something to say: the compiler
    # error above already names the header that could not be found, and it
    # is usually one reached *from* a public header rather than one listed
    # here. Printing the skipped ones every time would send the reader after
    # the wrong file.
    listed='1,/END STANDALONE CHECK/ s/^#include <\(wx\/[^>]*\)>/\1/p'
    missing=`sed -n "$listed" "$srcdir/tests/allheaders.h" |
        while read -r h; do
            case " $not_installed_ok " in
                *" $h "*) continue ;;
            esac
            [ -f "$incdir/$h" ] || echo "    $h"
        done`

    if [ -n "$missing" ]; then
        echo
        echo "Public headers listed in allheaders.h and not in $incdir:"
        echo "$missing"
    fi
    rc=1
fi

exit $rc
