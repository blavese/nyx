#!/usr/bin/env bash
# Several tasks at once, and one expensive check for all of them.
#
# cycle.sh does one task properly and takes about forty minutes, thirty-five
# of which is the full gate. Run eight tasks that way and it is most of a
# day, nearly all of it waiting for QEMU.
#
# Two things fix that.
#
# Each task gets its own git worktree, so three agents can write three
# changes at once without seeing each other's files. The fast gate runs in
# each worktree; it is only a build and two QEMU runs with no monitor port,
# so several at a time collide over nothing but the processor.
#
# Then the branches that passed are merged onto one integration branch and
# the full gate runs once for the batch rather than once per task. The full
# gate cannot be parallel anyway: shotcheck, termcheck and deskcheck each
# drive QEMU's monitor on a fixed port.
#
# Three tasks a batch turns three forty minute cycles into roughly one.
#
#   bash pipeline/batch.sh              three tasks
#   BATCH=2 bash pipeline/batch.sh      two
#   PUSH=1 bash pipeline/batch.sh       and push the batch when it lands

source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
source "$(dirname "${BASH_SOURCE[0]}")/models.sh"

BATCH="${BATCH:-1}"
PUSH="${PUSH:-0}"
WORKTREES="$ROOT/../nyx-worktrees"

cd "$ROOT"
if ! git diff --quiet || ! git diff --cached --quiet; then
  die "the working tree has uncommitted changes; sort those out first"
fi
[ "$(git rev-parse --abbrev-ref HEAD)" = main ] || die "not on main"

# --- claim the tasks up front ---------------------------------------------
#
# Marked "doing" before anything starts, so a second batch run cannot pick
# the same ones up.

ids=(); authors=(); titles=()
for ((i = 0; i < BATCH; i++)); do
  line="$(backlog_next)"
  [ -n "$line" ] || break
  id="${line%%|*}"; rest="${line#*|}"
  ids+=("$id"); authors+=("${rest%%|*}"); titles+=("${rest#*|}")
  backlog_set "$id" "doing"
done

[ "${#ids[@]}" -gt 0 ] || { echo "backlog: nothing to do"; exit 0; }

echo "=== batch of ${#ids[@]} ==="
for ((i = 0; i < ${#ids[@]}; i++)); do
  echo "  ${ids[$i]}  (${authors[$i]} writes, $( [ "${authors[$i]}" = claude ] && echo codex || echo claude ) reviews)"
done
echo

# The claim is a change to a tracked file; commit it so the worktrees below
# branch from a clean main rather than inheriting a dirty tree.
git add pipeline/backlog.md
git commit -qm "pipeline: claim ${ids[*]}" || true

# --- one task, in a worktree of its own -----------------------------------
#
# Everything up to and including the fast gate. The full gate is not here:
# it belongs to the batch.

one_task() {
  local id="$1" author="$2" title="$3"
  author="$(author_for "$author")"
  local reviewer; reviewer="$(reviewer_for "$author")"

  local wt="$WORKTREES/$id"
  local dir="$STATE/$id"
  mkdir -p "$dir"
  local CYCLE_LOG="$dir/log.txt"
  : > "$CYCLE_LOG"

  say() { printf '%s  [%s] %s\n' "$(date +%H:%M:%S)" "$id" "$*" | tee -a "$CYCLE_LOG"; }

  git worktree remove --force "$wt" >/dev/null 2>&1
  git branch -D "pipeline/$id" >/dev/null 2>&1
  git worktree add -q -b "pipeline/$id" "$wt" main || { say "no worktree"; return 1; }

  # The agents are pointed at the worktree, not the repository.
  local saved_root="$ROOT"
  ROOT="$wt"

  say "writing"
  local prompt="$dir/implement.prompt"
  sed "s|{{TASK}}|$title|" "$PIPE/prompts/implement.md" > "$prompt"

  if [ "$author" = codex ]; then
    run_codex "$prompt" "$dir/author-report.txt" $(codex_args_for implement) \
        > "$dir/author-stdout.txt" 2>&1
  else
    run_claude "$prompt" "$dir/author-report.txt" $(claude_args_for)
  fi
  local rc=$?
  ROOT="$saved_root"

  [ "$rc" -eq 0 ] || { say "$author exited $rc"; return 1; }
  ( cd "$wt" && git diff --quiet ) && { say "$author changed nothing"; return 1; }
  say "changed: $(cd "$wt" && git diff --name-only | tr '\n' ' ')"

  say "fast gate"
  ( cd "$wt" && bash "$PIPE/gate.sh" fast ) > "$dir/gate-fast.txt" 2>&1 || {
    say "failed the fast gate"; tail -6 "$dir/gate-fast.txt" >> "$CYCLE_LOG"; return 1; }

  say "review by $reviewer"
  ROOT="$wt"
  local rprompt="$dir/review.prompt"
  python - "$PIPE/prompts/review.md" "$rprompt" "$title" "$dir/author-report.txt" <<'PY'
import io, sys
tpl, out, task, report = sys.argv[1:5]
s = io.open(tpl, encoding="utf-8").read()
s = s.replace("{{TASK}}", task)
s = s.replace("{{REPORT}}", io.open(report, encoding="utf-8", errors="replace").read())
io.open(out, "w", encoding="utf-8").write(s)
PY

  if [ "$reviewer" = codex ]; then
    run_codex "$rprompt" "$dir/review.json" $(codex_args_for review) \
        > "$dir/review-stdout.txt" 2>&1
  else
    run_claude "$rprompt" "$dir/review.json" $(claude_args_for)
  fi
  ROOT="$saved_root"

  python "$PIPE/parse_review.py" "$dir/review.json" "$dir/findings.json" > "$dir/verdict.txt" 2>&1
  local n_high
  n_high="$(python -c "
import json,sys
f=json.load(open(sys.argv[1]))['findings']
print(sum(1 for x in f if x.get('severity')=='high'))" "$dir/findings.json" 2>/dev/null || echo 0)"
  say "review: $(cat "$dir/verdict.txt")"

  if [ "${n_high:-0}" -gt 0 ]; then
    say "answering $n_high serious finding(s)"
    ROOT="$wt"
    local aprompt="$dir/address.prompt"
    python - "$PIPE/prompts/address.md" "$aprompt" "$title" "$dir/findings.json" <<'PY'
import io, sys
tpl, out, task, findings = sys.argv[1:5]
s = io.open(tpl, encoding="utf-8").read()
s = s.replace("{{TASK}}", task)
s = s.replace("{{FINDINGS}}", io.open(findings, encoding="utf-8", errors="replace").read())
io.open(out, "w", encoding="utf-8").write(s)
PY
    if [ "$author" = codex ]; then
      run_codex "$aprompt" "$dir/address.txt" $(codex_args_for address) >/dev/null 2>&1
    else
      run_claude "$aprompt" "$dir/address.txt" $(claude_args_for)
    fi
    ROOT="$saved_root"

    say "fast gate again"
    ( cd "$wt" && bash "$PIPE/gate.sh" fast ) > "$dir/gate-fast-2.txt" 2>&1 || {
      say "the answers broke the fast gate"; return 1; }
  fi

  # Committed on its own branch. Nothing has reached main yet.
  ( cd "$wt" && git add -A && git commit -q -F - <<EOF
$title

$(head -c 1000 "$dir/author-report.txt")

Written by $author, reviewed by $reviewer.
EOF
  ) || { say "nothing to commit"; return 1; }

  say "ready to integrate"
  return 0
}

# --- run them at once ------------------------------------------------------

pids=(); results=()
for ((i = 0; i < ${#ids[@]}; i++)); do
  one_task "${ids[$i]}" "${authors[$i]}" "${titles[$i]}" &
  pids+=($!)
done

for ((i = 0; i < ${#pids[@]}; i++)); do
  if wait "${pids[$i]}"; then results+=(ok); else results+=(no); fi
done

echo
ready=()
for ((i = 0; i < ${#ids[@]}; i++)); do
  if [ "${results[$i]}" = ok ]; then
    ready+=("${ids[$i]}")
  else
    backlog_set "${ids[$i]}" "blocked"
    echo "  ${ids[$i]}: blocked, see pipeline/state/${ids[$i]}/log.txt"
  fi
done

[ "${#ready[@]}" -gt 0 ] || { git add -A; git commit -qm "pipeline: nothing landed" || true; echo "nothing to integrate"; exit 1; }

# --- put them together and check the lot once -----------------------------

echo
echo "=== integrating ${#ready[@]} ==="
git branch -D pipeline/integration >/dev/null 2>&1
git checkout -q -b pipeline/integration main

landed=()
for id in "${ready[@]}"; do
  if git merge -q --no-ff "pipeline/$id" -m "$id" 2>/dev/null; then
    landed+=("$id"); echo "  merged $id"
  else
    git merge --abort 2>/dev/null
    backlog_set "$id" "todo"          # not its fault; try it against a newer main
    echo "  $id conflicts with the batch, put back on the list"
  fi
done

echo
echo "=== full gate, once, for the batch ==="
if bash "$PIPE/gate.sh" full > "$STATE/gate-full-batch.txt" 2>&1; then
  echo "  passed"
  git checkout -q main
  git merge -q --ff-only pipeline/integration
  for id in "${landed[@]}"; do backlog_set "$id" "done"; done
  git add -A && git commit -qm "pipeline: ${landed[*]} landed" || true
  echo "landed: ${landed[*]}"
  [ "$PUSH" = 1 ] && git push -q origin main && echo "pushed"
else
  tail -14 "$STATE/gate-full-batch.txt" | sed 's/^/    /'
  echo
  echo "  the batch does not pass together. Falling back to one at a time,"
  echo "  which is slower but tells us which one it was."
  git checkout -q main
  for id in "${landed[@]}"; do
    echo "  trying $id alone"
    git checkout -q -b "pipeline/solo-$id" main
    git merge -q --no-ff "pipeline/$id" -m "$id"
    if bash "$PIPE/gate.sh" full > "$STATE/gate-full-$id.txt" 2>&1; then
      git checkout -q main && git merge -q --ff-only "pipeline/solo-$id"
      backlog_set "$id" "done"; echo "    $id passes alone, landed"
    else
      git checkout -q main; git branch -D "pipeline/solo-$id" >/dev/null 2>&1
      backlog_set "$id" "blocked"; echo "    $id is the problem, blocked"
    fi
  done
  git add -A && git commit -qm "pipeline: after splitting the batch" || true
fi

# --- tidy ------------------------------------------------------------------
for id in "${ids[@]}"; do
  git worktree remove --force "$WORKTREES/$id" >/dev/null 2>&1
done
git worktree prune >/dev/null 2>&1
echo
echo "left to do: $(backlog_count_todo)"
