#!/usr/bin/env bash
# Checks the things a run needs before it starts spending an hour finding out.
#
# The expensive failure is not a task going wrong. It is a task going wrong
# for a reason that would have stopped every task: an agent whose login has
# expired, a missing emulator, a dirty tree. Each of those turns the whole
# backlog into blocked tasks one cycle at a time, and each is a second to
# check up front.
#
#   bash pipeline/preflight.sh

source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

problems=0
ok()   { printf '  ok    %s\n' "$1"; }
bad()  { printf '  BAD   %s\n' "$1"; problems=$((problems + 1)); }
note() { printf '        %s\n' "$1"; }

echo "=== preflight ==="

# --- the two agents --------------------------------------------------------
#
# Asked something trivial with a real answer, because "the binary exists" and
# "the binary will do work for you" are different questions and only the
# second one matters.

if [ -x "$CODEX" ]; then
  # A real file, not /dev/stdout: this is a Windows binary and it cannot
  # open the paths a unix shell offers for that.
  probe="$(mktemp)"
  timeout 180 "$CODEX" exec --cd "$ROOT" --sandbox read-only \
      --skip-git-repo-check -o "$probe" "Reply with exactly: ready" >/dev/null 2>&1
  reply="$(cat "$probe" 2>/dev/null)"
  rm -f "$probe"
  if printf '%s' "$reply" | grep -qi ready; then ok "codex answers"
  else
    bad "codex did not answer"
    note "$(printf '%s' "$reply" | head -2)"
  fi
else
  bad "codex not found"
  note "looked under AppData/Local/OpenAI/Codex/bin"
fi

if command -v claude >/dev/null 2>&1 || [ -x "$CLAUDE" ]; then
  reply="$(timeout 180 claude -p "Reply with exactly: ready" </dev/null 2>&1 | tail -3)"
  if printf '%s' "$reply" | grep -qi ready; then ok "claude answers"
  else
    bad "claude did not answer"
    note "$(printf '%s' "$reply" | head -2)"
    printf '%s' "$reply" | grep -qi "authenticate\|401\|expired" && \
      note "run 'claude' once and sign in, then try again"
  fi
else
  bad "claude not found"
fi

# --- what the gate needs ---------------------------------------------------

QEMU="${QEMU:-/c/Program Files/qemu/qemu-system-x86_64.exe}"
[ -x "$QEMU" ] || QEMU="$(command -v qemu-system-x86_64 || echo "$QEMU")"
[ -x "$QEMU" ] && ok "qemu is there" || bad "qemu not found: the gate cannot run"

command -v python >/dev/null 2>&1 && ok "python is there" || bad "python not found"

# --- the repository --------------------------------------------------------

cd "$ROOT"
if git diff --quiet && git diff --cached --quiet; then
  ok "the tree is clean"
else
  bad "the tree has uncommitted changes"
  note "$(git status --porcelain | head -3 | tr '\n' ' ')"
fi

branch="$(git rev-parse --abbrev-ref HEAD)"
[ "$branch" = main ] && ok "on main" || bad "on '$branch', not main"

todo="$(backlog_count_todo)"
[ "$todo" -gt 0 ] && ok "$todo task(s) to do" || bad "the backlog has nothing marked todo"

echo
if [ "$problems" -eq 0 ]; then echo "preflight: ready"; exit 0; fi
echo "preflight: $problems thing(s) to sort out first"
exit 1
