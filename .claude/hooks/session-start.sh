#!/bin/bash
# Fork-only (see #112): prepares a Claude Code cloud container to build the
# port and run its test suites the way CI's "wxGTK 4" job does.
set -euo pipefail

if [ "${CLAUDE_CODE_REMOTE:-}" != "true" ]; then
  exit 0
fi

cd "${CLAUDE_PROJECT_DIR:-$(dirname "$0")/../..}"

# CI checks out every submodule recursively. Without them configure falls
# back to system libraries, or leaves features out, and the build differs.
git submodule update --init --recursive --depth 1

# The packages CI installs for the GTK4 job, the GTK+ 3 headers for a
# control build, and XTest. GitHub's runners have XTest preinstalled; without it configure
# silently disables wxUIActionSimulator and every test that synthesises
# input is compiled out of test_gui.
wxGTK_VERSION=4 WX_EXTRA_PACKAGES="libxtst-dev libgtk-3-dev" \
    ./build/tools/before_install.sh

# What CI's Xvfb jobs set; see CLAUDE.md.
if [ -n "${CLAUDE_ENV_FILE:-}" ]; then
  echo 'export wxUSE_XVFB=1' >> "$CLAUDE_ENV_FILE"
  echo 'export NO_AT_BRIDGE=1' >> "$CLAUDE_ENV_FILE"
fi
