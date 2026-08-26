#!/bin/bash
# Drives one wxAUI dock round trip with real pointer input and reports what
# the pane's own state says afterwards. Used by issues #134 and #150.
#
#   aui-dock-roundtrip.sh x11        under a private Xvfb + openbox
#   aui-dock-roundtrip.sh wayland    under an already-running compositor
#
# Build the two binaries it drives first, and point the variables below at
# them if they are not where this expects. A stale auidock is worse than no
# auidock: after a header change it reads the manager's fields at the old
# offsets and dies somewhere unrelated (see #150), so rebuild it whenever
# include/wx/aui/ has moved.
#
#   WXBUILD=/path/to/build   the wx build to run against
#   AUIDOCK, XDRAG, WLDRAG   the probe and the input injectors
#
#   g++ -o $AUIDOCK aui-dock-roundtrip.cpp $($WXBUILD/wx-config --cxxflags \
#       --libs core,base,aui)
#   gcc -o $XDRAG xdrag.c -lX11 -lXtst        # X11: XTEST
#   gcc -o $WLDRAG wldrag.c ...               # Wayland: see wldrag.c
WXBUILD=${WXBUILD:-/home/user/wxbuild-gtk4-ci}
AUIDOCK=${AUIDOCK:-/tmp/auidock}
XDRAG=${XDRAG:-/tmp/xdrag}
WLDRAG=${WLDRAG:-/tmp/wldrag}

if [ ! -x "$AUIDOCK" ]; then
  echo "  no $AUIDOCK -- build it first, see the header of this script"
  exit 1
fi

if [ "$1" = "x11" ]; then
  D=":$((110 + RANDOM % 40))"
  Xvfb $D -screen 0 1280x1024x24 -nolisten tcp >/dev/null 2>&1 &
  sleep 2
  DISPLAY=$D openbox >/dev/null 2>&1 & sleep 2
  export DISPLAY=$D GDK_BACKEND=x11; unset WAYLAND_DISPLAY
  DRAG="env DISPLAY=$D $XDRAG"
else
  export XDG_RUNTIME_DIR=/tmp/xdgrt WAYLAND_DISPLAY=wayland-1
  export GDK_BACKEND=wayland
  unset DISPLAY
  DRAG="$WLDRAG"
fi
export LD_LIBRARY_PATH=$WXBUILD/lib
LOG=/tmp/dockrun-$1.log
( timeout 32 $AUIDOCK > $LOG 2>&1 ) &
APP=$!
sleep 5
read AX AY <<< "$(grep -m1 '^AIM ' $LOG | awk '{print $2,$3}')"
read CX CY CW CH <<< "$(grep -m1 '^CLIENT ' $LOG | awk '{print $2,$3,$4,$5}')"
if [ -z "$AX" ]; then
  echo "  no geometry reported -- app did not start"
  kill $APP 2>/dev/null
  exit 1
fi
TX=$((CX + 15)); TY=$((CY + CH/2))
echo "  caption $AX,$AY | client $CX,$CY ${CW}x${CH} | drop $TX,$TY"
# Dwell on the drop point with small movements: wxAUI decides where a pane
# would land from the motion events, and shows a hint, before the release
# acts on it. A single jump to the target and an immediate release often
# arrives before that has happened.
$DRAG move $AX $AY sleep 400 down sleep 600 \
  move $((CX+CW/2)) $((CY+CH/3)) sleep 350 \
  move $((CX+CW/3)) $TY sleep 350 \
  move $((TX+80)) $TY sleep 350 \
  move $((TX+8)) $TY sleep 300 \
  move $TX $((TY+3)) sleep 300 \
  move $((TX+2)) $((TY-2)) sleep 300 \
  move $TX $TY sleep 900 up sleep 500 >/dev/null 2>&1
wait $APP
grep -E "STATE|CHECK|RESULT" $LOG
[ "$1" = "x11" ] && pkill -f "Xvfb $D" 2>/dev/null
true
