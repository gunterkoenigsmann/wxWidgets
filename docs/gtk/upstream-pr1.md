# The first pull request, and what it is honestly made of

The plan is to send step 1 of the series on its own and see what comes back,
rather than to open eighteen pull requests at once. What follows is the text
for it and, first, the thing about it a reviewer will notice and that should
therefore be said by us rather than found by them.

## Step 1 is not a set of cross-platform fixes

It is *the port's changes that live outside the GTK backend*, which is not the
same thing. Measured on the branch as it stands:

| | files | added lines |
|---|---:|---:|
| whole step | 43 | 933 |
| of which mention `__WXGTK4__`, `wxGTK4` or GTK4 | 32 | ~790 |
| free of any GTK4 reference | 11 | 142 |

So roughly **eight lines in ten are conditional on a toolkit upstream's tree
does not have yet**. They compile — every one is inside `#ifdef __WXGTK4__`,
which nothing defines until step 17 turns the switch on — but a reviewer
opening this cold sees a large diff whose reason arrives in a later patch.

That is a consequence of splitting by subsystem, which is what was asked for,
and it is not a fault in the split. It does mean the covering text has to say
what the guards are for, or the first impression is "dead code".

The alternative — sending only the 142 lines that stand on their own — was
considered and is worse: too small to tell us anything about how a series of
this size will be received, and several of those changes only make sense
alongside the port anyway.

## What is already known to work

Three fixes from this fork are in upstream master already, two of them
committed by maintainers themselves after the analysis was posted. So the
question this pull request asks is not "are these changes any good" but "is
this shape of submission workable".

## The text

---

**GTK4 port, step 1 of 18: the changes outside the GTK backend**

This is the first of a series splitting a GTK4 port of wxGTK into
review-sized pieces, as asked for in the discussion of the original single
patch. The whole port is about 31,000 lines; this step is 933.

It contains every change the port makes **outside** `src/gtk/` and
`include/wx/gtk/`, and it comes first deliberately: these are the only files
in the series that other ports also compile, so if this step is acceptable
nothing after it can affect MSW, macOS or Qt.

Most of it is inside `#ifdef __WXGTK4__` and does nothing until step 17 adds
the `--with-gtk=4` switch: the guards are forward references to the rest of
the series rather than code that runs today. The rest are ordinary fixes that
stand on their own — a caret that is not redrawn after a native overlay is
reset, a property grid page whose toolbar is realized without recalculating
positions, and similar.

**Order of the series.** Shared changes first, then the private headers, the
window and event plumbing, the subsystems, tests and samples, the build
system, and the CI job last and separately so it can be declined without
affecting the port.

**What has been measured.** Each step builds on its own, from an empty build
directory: all eighteen under GTK+ 3, and steps 1 to 16 under Qt as well — a
toolkit with no GTK in it, which is the relevant control for this step in
particular. With the whole series applied, the GUI test suite passes 917
cases under GTK4 and 902 under GTK+ 3; pristine master passes 878 in the same
container.

**What has not been measured.** MSW and macOS, for want of the toolchains.
That is the main reason for sending this step first and alone.

---

## Practicalities

The branch is `upstream-series/01-shared` in this fork. It applies to master
at `0820518`. If the reception is good the remaining seventeen follow in
order; if the shape is wrong, only one patch has to be re-cut.
