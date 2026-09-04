#!/usr/bin/env python3
"""Cut the GTK4 port into a series of review-size branches.

Upstream asked for this rather than one patch:

    I don't have any experience with this, but could you please ask your LLM
    to create a series of PRs, with each one being of reasonable size? [...]
    Personally I'm not ready to merge a 50000 line patch without review but
    neither am I capable of actually reviewing it. Reviewing 10 5000 line PRs
    might be doable, however.

                                                                 -- #175

So this splits by subsystem rather than by commit. Each branch takes the
*final* state of one group of files from the port branch and stacks on the
one before it, which means the series has no intermediate states that were
never tested: every step is the port's own code, and the last step is the
port.

Two things decide the order.

  * The changes outside the GTK backend come first. They are the ones that
    could affect Windows, macOS or Qt, they are the smallest group, and a
    maintainer should be able to see all of them before anything else.

  * The build system comes last. It is what lists the new source files and
    turns the GTK4 switch on, so until it lands the existing GTK+ 2 and
    GTK+ 3 builds compile exactly as before -- the new files are simply not
    built. Putting it first would break the build at step one, which is how
    the first version of this script was found to be wrong.

docs/ and CLAUDE.md are left out: they are this fork's working notes.

Usage:
    build/tools/make-upstream-series.py --dry   # what the groups would be
    build/tools/make-upstream-series.py         # create the branches

Check afterwards that nothing was dropped:
    git diff upstream-series/17-build <port branch> -- . ':!docs' ':!CLAUDE.md'

and that every step still builds, which is what catches a file landing in a
later group than the code calling it:
    build/tools/build-upstream-series.sh
"""

import subprocess, collections, sys, os
import re

# What the CI jobs run, kept with them so the step can be dropped whole.
CI_RUNS = (
    "build/tools/before_install.sh",
    "build/tools/check-gtk-min-versions.py",
    "build/tools/gtk4-invariants.c",
    "misc/scripts/check_configure.sh",
    "misc/scripts/check_files_lists.sh",
)

# This fork's own tooling, which has no meaning in upstream's tree: the two
# scripts that produce and check this very series, and the commit attribution
# check from #177 whose CI job is stripped with the rest of the fork-only ones.
FORK_ONLY_FILES = (
    "build/tools/make-upstream-series.py",
    "build/tools/build-upstream-series.sh",
    "build/tools/check-commit-trailers.py",
)

# Steps whose branch is written by hand rather than cut by this script.
#
# Upstream asked for step 1 to be a series of commits, one per reason, because
# what it collects really is unrelated changes that happen to share the
# property of being outside src/gtk. A generated branch cannot say why each
# change was made, so that one is maintained as a branch of its own; the cut
# still regenerates its content and refuses to go on if the two have drifted
# apart.
HAND_SPLIT = ("01-shared",)

MB  = "0820518c97a13d0905a6e8af16b203d307586107"
TIP = "gtk4-project/claude/gtk4-wxwidgets-port-plan-pwo52u"

def gtk(p, *names):
    return any(p.startswith("src/gtk/"+n) or p.startswith("include/wx/gtk/"+n) for n in names)

# order matters: first match wins
RULES = [
 ("01-shared",     "GTK4: the changes outside the GTK backend",
   # src/unix and include/wx/unix are not "outside the backend" in the sense
   # that matters here: they are built only for the Unix ports and their
   # changes call into it -- wxGetKeyStateGTK() in utilsx11.cpp uses
   # wxGetTopLevelGdkDisplay(), which src/gtk/window.cpp defines in step 3.
   # A step that has the caller and not the callee does not link.
   lambda p: not (p.startswith("src/gtk/") or p.startswith("include/wx/gtk/")
                  or p.startswith("src/unix/")
                  or p.startswith("include/wx/unix/")
                  or p.startswith("tests/") or p.startswith("samples/")
                  or p.startswith("demos/") or p.startswith("build/")
                  or p.startswith(".github/") or p.startswith("misc/")
                  or p in ("configure","configure.ac","Makefile.in","CLAUDE.md"))),
 ("02-private",    "GTK4: private headers and the GTK+ 3 compatibility shim",
   # private.h as well as private/: window.cpp calls
   # wxGtkScrollbarGetAdjustment() from it, so a step that carries window.cpp
   # without it does not compile.
   lambda p: p.startswith("include/wx/gtk/private/")
             or p == "include/wx/gtk/private.h"),
 ("03-core",       "GTK4: wxWindow, the event loop and the wxPizza container",
   lambda p: p in ("src/gtk/window.cpp","src/gtk/win_gtk.cpp","src/gtk/evtloop.cpp","src/gtk/app.cpp","src/gtk/utilsgtk.cpp","src/gtk/private.cpp","include/wx/gtk/window.h","include/wx/gtk/app.h","include/wx/gtk/evtloop.h")),
 ("04-toplevel",   "GTK4: top level windows, frames, dialogs and popups",
   lambda p: gtk(p,"toplevel","popupwin","minifram","nonownedwnd","frame","dialog","mdi")),
 ("05-drawing",    "GTK4: device contexts, the renderer and overlays",
   lambda p: gtk(p,"dc","renderer","overlay","bitmap","brush","pen","colour","cursor","icon","image","region","graphics")),
 ("06-menus",      "GTK4: menus on GMenuModel and GAction",
   lambda p: gtk(p,"menu","accel","assertdlg")),
 ("07-text",       "GTK4: text entry and text control",
   lambda p: gtk(p,"textctrl","textentry","spinctrl","srchctrl","combobox")),
 ("08-items",      "GTK4: item containers",
   lambda p: gtk(p,"listbox","choice","dataview","radiobox","checklst","listctrl","treectrl","treeentry","bmpcbox")),
 ("09-controls",   "GTK4: the remaining controls",
   lambda p: gtk(p,"anybutton","button","radiobut","slider","toolbar","statusbar","notebook","gauge","scrolbar","scrolwin","spinbutt","statbmp","statbox","stattext","tglbtn","filepicker","clrpicker","fontpicker","calctrl","collpane","infobar","headerctrl","checkbox","control","animate","hyperlink","artgtk","settings","font","sockgtk","timer","tooltip","aboutdlg","display","glcanvas","activityindicator")),
 ("10-dialogs",    "GTK4: the standard dialogs",
   lambda p: gtk(p,"dirdlg","filedlg","msgdlg","print","colordlg","fontdlg","filectrl","progdlg")),
 ("11-clipboard",  "GTK4: clipboard and drag and drop",
   lambda p: gtk(p,"clipbrd","dnd")),
 ("12-a11y",       "GTK4: accessibility",
   lambda p: gtk(p,"access")),
 ("13-taskbar",    "GTK4: the taskbar icon and the status notifier",
   lambda p: gtk(p,"taskbar","statusnotifier","dbusmenu","appindicator")),
 ("14-webview",    "GTK4: wxWebView on WebKitGTK 6",
   lambda p: gtk(p,"webview")),
 ("15-rest",       "GTK4: the last of the backend",
   lambda p: p.startswith("src/gtk/") or p.startswith("include/wx/gtk/")),
 ("16-tests",      "GTK4: tests and samples",
   lambda p: p.startswith("tests/") or p.startswith("samples/") or p.startswith("demos/")),
 ("17-build",      "GTK4: the build system and the configure switch",
   lambda p: p not in CI_RUNS
             and (p in ("configure","configure.ac","Makefile.in")
                  or p.startswith("build/") or p.startswith("misc/"))),
 # Last, and on its own, because upstream may not want a GTK4 job before the
 # port itself: dropping this step drops the CI and leaves the port whole.
 ("18-ci",         "GTK4: a CI job for the new toolkit",
   lambda p: p.startswith(".github/") or p in CI_RUNS),
]

out = subprocess.run(["git","diff","--numstat",MB,TIP],capture_output=True,text=True).stdout
files = collections.defaultdict(list)
for line in out.splitlines():
    a,d,p = line.split("\t")
    if p.startswith("docs/") or p == "CLAUDE.md": continue
    if p in FORK_ONLY_FILES: continue
    for name,_desc,fn in RULES:
        if fn(p):
            files[name].append(p); break
    else:
        files["15-rest"].append(p)

if "--dry" in sys.argv:
    for name,desc,_ in RULES:
        print("%-13s %3d Dateien  %s" % (name, len(files[name]), desc))
    sys.exit()

FORK_BEGIN = "# Fork only, not for upstream"
FORK_END   = "# End fork only."
UPSTREAM_HAS = "# Upstream has: "

def strip_fork_only(path):
    """Take out what this fork needs and upstream does not.

    A region runs from a FORK_BEGIN comment to a FORK_END one. If a line
    inside it says "# Upstream has: X", the region is replaced by X at the
    region's indentation -- that is for the places where the fork's version
    reformatted something rather than only adding to it. Otherwise the region
    is simply removed.
    """
    try:
        text = open(path, encoding="utf-8").read()
    except (OSError, UnicodeDecodeError):
        return False
    if FORK_BEGIN not in text:
        return False

    out, skipping, replacement, indent = [], False, None, ""
    justClosed = False
    for line in text.split("\n"):
        if not skipping and FORK_BEGIN in line:
            skipping = True
            indent = line[:len(line) - len(line.lstrip())]
            replacement = None
            continue
        if skipping:
            if UPSTREAM_HAS in line:
                replacement = line.split(UPSTREAM_HAS, 1)[1]
            if FORK_END in line:
                skipping = False
                if replacement is not None:
                    out.append(indent + replacement)
                else:
                    justClosed = True
            continue
        # Removing a whole block leaves the blank line above it and the one
        # below it next to each other, which is a diff of its own.
        if justClosed and not line.strip() and out and not out[-1].strip():
            justClosed = False
            continue
        justClosed = False
        out.append(line)

    if skipping:
        print("FEHLER: unbeendete Fork-only-Region in", path)
        sys.exit(1)

    open(path, "w", encoding="utf-8").write("\n".join(out))
    return True


# Things that mean something in this fork and nothing, or something wrong, in
# upstream's tree.
#
# This exists because a cut is not reviewed line by line before it is sent: the
# first PR went out carrying the WXAUI_DRAGLOG logging and its "see #112", and
# nothing between writing it and pushing it looked. A grep does look, every
# time.
#
# The first list stops the cut, as those can only be fork-only leftovers. The
# second only reports, because an issue number has to be rewritten by hand into
# what it was saying -- upstream cannot follow a link into this fork -- and
# that is a job for whoever prepares the step, not for the cut.
FATAL_TRACES = (
    re.compile(r"Fork only, not for upstream"),
    re.compile(r"End fork only\."),
    re.compile(r"WXAUI_DRAGLOG"),
)
WARN_TRACES = (
    re.compile(r"\b(?:see|issue)\s+#[0-9]+", re.I),
)

def check_no_fork_traces(paths, step):
    """Look for fork-only references in what this step adds.

    Only lines the series actually changes are looked at, so a reference
    upstream itself wrote stays upstream's business.
    """
    fatal, warn = [], []
    for p in paths:
        if not os.path.exists(p) or p in FORK_ONLY_FILES:
            continue
        diff = subprocess.run(["git", "diff", "-U0", MB, "--", p],
                              capture_output=True, text=True).stdout
        for line in diff.split("\n"):
            if not line.startswith("+") or line.startswith("+++"):
                continue
            where = "%s: %s" % (p, line[1:].strip())
            if any(rx.search(line) for rx in FATAL_TRACES):
                fatal.append(where)
            elif any(rx.search(line) for rx in WARN_TRACES):
                warn.append(where)
    for w in warn:
        print("   WARNUNG: Fork-Issue-Referenz in %s:" % step, w)
    if fatal:
        print("FEHLER: Fork-only-Spuren in", step)
        for b in fatal:
            print("   ", b)
        sys.exit(1)


def run(*args, **kw):
    r = subprocess.run(args, capture_output=True, text=True, **kw)
    if r.returncode:
        print("FEHLER:", " ".join(args), r.stderr[:400]); sys.exit(1)
    return r.stdout

prev = MB
for name, desc, _ in RULES:
    paths = files[name]
    if not paths:
        print("übersprungen (leer):", name); continue
    branch = "upstream-series/" + name
    handSplit = name in HAND_SPLIT
    # For a hand-split step the content is still cut here, but into a scratch
    # branch, and only to compare it against the branch that is kept by hand.
    target = branch + ".regen" if handSplit else branch
    if target in run("git","branch","--list",target):
        run("git","branch","-D",target)
    run("git","checkout","-q","-b",target,prev)
    # take this group's final state from the port branch
    for i in range(0, len(paths), 60):
        run("git","checkout",TIP,"--",*paths[i:i+60])
    # The fork's own CI triggers and checks stay here and do not go upstream;
    # see #112. This is what makes the grep in that issue return nothing.
    for p in paths:
        # Only the workflows carry these, and restricting it here keeps the
        # script from stripping its own marker constants when it lands in
        # 17-build.
        if p.startswith(".github/") and os.path.exists(p):
            strip_fork_only(p)
    check_no_fork_traces(paths, name)
    msg = ("%s\n\n"
           "One step of the GTK4 port, split for review as upstream asked.\n"
           "The series is cumulative: this applies on top of %s and the whole\n"
           "port is the last step of it.\n\n"
           "Co-authored-by: Claude Opus 5 <noreply@anthropic.com>\n") % (desc, prev if prev==MB else "the previous step")
    run("git","commit","-q","-a","-m",msg)
    if handSplit:
        cut  = run("git","rev-parse",target+"^{tree}").strip()
        kept = run("git","rev-parse",branch+"^{tree}").strip()
        if cut != kept:
            print("FEHLER: %s ist von Hand geschrieben und weicht ab:"
                  % branch)
            print(run("git","diff","--stat",branch,target))
            sys.exit(1)
        run("git","checkout","-q",branch)
        run("git","branch","-D",target)
    head = run("git","rev-parse","--short",branch).strip()
    print("%-30s %s  %3d Dateien%s"
          % (branch, head, len(paths), "  (von Hand)" if handSplit else ""))
    prev = branch
print("TIP:", prev)
