#!/usr/bin/env bash
# #*#*#*#*#*#*#*#*#*#*#*#*#*#*#*#*# install_hooks.sh *#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
#
# CDTAPI - Points git at the hooks kept in Scripts/hooks
#
# SPDX-License-Identifier: BSD-3-Clause

set -euo pipefail

RepoRoot="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$RepoRoot"

# core.hooksPath keeps the hooks in the repository, where they are reviewed and versioned,
# rather than copied into .git/hooks where they drift per clone.
git config core.hooksPath Scripts/hooks

echo "Hooks installed: core.hooksPath = Scripts/hooks"
echo "Run 'git config --unset core.hooksPath' to undo."
