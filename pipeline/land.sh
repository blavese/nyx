#!/usr/bin/env bash
# Puts an already written branch through the full gate and lands it.
#
# No agent runs at all, so it costs nothing but time, and time here is QEMU
# which is free. Use it when a batch was interrupted after the work was
# committed but before the gate finished, which is exactly what happens when
# a session ends underneath a running pipeline.
#
#   bash pipeline/land.sh pipeline/pipes
#   PUSH=1 bash pipeline/land.sh pipeline/pipes

source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

BRANCH="${1:-}"
[ -n "$BRANCH" ] || die "which branch? e.g. bash pipeline/land.sh pipeline/pipes"
git rev-parse --verify "$BRANCH" >/dev/null 2>&1 || die "no such branch: $BRANCH"

PUSH="${PUSH:-0}"
cd "$ROOT"
git diff --quiet && git diff --cached --quiet || die "the tree has uncommitted changes"
[ "$(git rev-parse --abbrev-ref HEAD)" = main ] || die "not on main"

n="$(git log --oneline "main..$BRANCH" | wc -l | tr -d ' ')"
[ "$n" -gt 0 ] || die "$BRANCH has nothing main does not already have"
echo "=== landing $BRANCH ($n commit(s)) ==="
git diff --stat main "$BRANCH" | tail -1

# On a branch of its own, so a gate failure leaves main exactly as it was.
TRY="pipeline/try-$(basename "$BRANCH")"
git branch -D "$TRY" >/dev/null 2>&1
git checkout -q -b "$TRY" main
if ! git merge -q --no-ff "$BRANCH" -m "$(basename "$BRANCH")"; then
  git merge --abort 2>/dev/null
  git checkout -q main; git branch -D "$TRY" >/dev/null 2>&1
  die "$BRANCH does not merge onto main cleanly"
fi

echo
if bash "$PIPE/gate.sh" full; then
  git checkout -q main
  git merge -q --ff-only "$TRY"
  git branch -D "$TRY" >/dev/null 2>&1

  id="$(basename "$BRANCH")"
  backlog_set "$id" "done"
  git add -A && git commit -qm "pipeline: $id landed" || true

  echo
  echo "landed on main"
  [ "$PUSH" = 1 ] && git push -q origin main && echo "pushed"
else
  git checkout -q main
  git branch -D "$TRY" >/dev/null 2>&1
  echo
  echo "the full gate says no. main is untouched and $BRANCH still has the work."
  exit 1
fi
