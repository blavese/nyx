#!/usr/bin/env bash
# One task, from the backlog to main.
#
# The shape of it:
#
#   1. take the next task, on a branch of its own
#   2. one agent writes the code
#   3. the gate says whether it works
#   4. the other agent reviews the diff
#   5. the author answers every finding, fixing or refusing with a reason
#   6. the gate again, in full, before anything reaches main
#
# The two agents never touch the tree at the same time; this script is what
# decides whose turn it is. Neither of them judges its own work, and neither
# can reach main without step six passing.
#
# Whoever wrote the code does not review it. That is the whole point of
# having two: the author's tests already passed, so the useful question is
# what those tests do not look at, and the person who wrote them is the
# worst placed to answer it.

source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
source "$(dirname "${BASH_SOURCE[0]}")/models.sh"

MAX_REPAIRS="${MAX_REPAIRS:-2}"
PUSH="${PUSH:-0}"                 # off unless asked: main is a public repo

# --- pick up the next thing to do -----------------------------------------

task_line="$(backlog_next)"
[ -n "$task_line" ] || { echo "backlog: nothing to do"; exit 0; }

TASK_ID="${task_line%%|*}"
rest="${task_line#*|}"
AUTHOR="${rest%%|*}"
TITLE="${rest#*|}"

case "$AUTHOR" in
  claude) REVIEWER=codex  ;;
  codex)  REVIEWER=claude ;;
  *) die "task $TASK_ID has no usable author (got '$AUTHOR')" ;;
esac

CYCLE_DIR="$STATE/$TASK_ID"
mkdir -p "$CYCLE_DIR"
CYCLE_LOG="$CYCLE_DIR/log.txt"
: > "$CYCLE_LOG"

log "task    $TASK_ID  $TITLE"
log "author  $AUTHOR, reviewed by $REVIEWER"

# --- a branch of its own ---------------------------------------------------
#
# Nothing half finished ever sits on main, and a task that goes wrong is one
# branch deletion away from never having happened.

cd "$ROOT"
if ! git diff --quiet || ! git diff --cached --quiet; then
  die "the working tree has uncommitted changes; sort those out first"
fi

BRANCH="pipeline/$TASK_ID"
git rev-parse --verify "$BRANCH" >/dev/null 2>&1 && git branch -D "$BRANCH" >/dev/null 2>&1
git checkout -q -b "$BRANCH" || die "could not branch"
backlog_set "$TASK_ID" "doing"

abandon() {
  log "abandoning: $1"
  cd "$ROOT"
  git checkout -q -- . 2>/dev/null
  git clean -qfd 2>/dev/null
  git checkout -q main 2>/dev/null
  git branch -D "$BRANCH" >/dev/null 2>&1
  backlog_set "$TASK_ID" "blocked"
  log "task $TASK_ID left blocked; the log is at $CYCLE_LOG"
  exit 1
}

# --- 2. write the code -----------------------------------------------------

prompt_from() {                     # prompt_from <template> <out> [k=v ...]
  local tpl="$1" out="$2"; shift 2
  cp "$PIPE/prompts/$tpl" "$out"
  local pair key val
  for pair in "$@"; do
    key="${pair%%=*}"; val="${pair#*=}"
    python - "$out" "$key" "$val" <<'PY'
import io, sys
path, key, val = sys.argv[1], sys.argv[2], sys.argv[3]
s = io.open(path, encoding="utf-8").read()
io.open(path, "w", encoding="utf-8").write(s.replace("{{" + key + "}}", val))
PY
  done
}

export FAKE_ROLE=author
log "writing the change"
prompt_from implement.md "$CYCLE_DIR/implement.prompt" "TASK=$TITLE"

if [ "$AUTHOR" = codex ]; then
  run_codex "$CYCLE_DIR/implement.prompt" "$CYCLE_DIR/author-report.txt" \
      $(codex_args_for implement) > "$CYCLE_DIR/author-stdout.txt" 2>&1
else
  run_claude "$CYCLE_DIR/implement.prompt" "$CYCLE_DIR/author-report.txt" \
      $(claude_args_for)
fi
author_rc=$?
[ "$author_rc" -eq 0 ] || abandon "$AUTHOR exited $author_rc"
[ -s "$CYCLE_DIR/author-report.txt" ] || abandon "$AUTHOR produced nothing"

# The backlog was marked "doing" a moment ago and the state directory is
# ours, so neither counts as the author having done anything. Without
# excluding them, a run where the agent wrote no code looks like a change.
# status, not diff: this very task added kernel/rtc.c and include/rtc.h,
# and neither showed up in `git diff` because they were not tracked yet. A
# task whose whole output is new files would look like an agent that sat
# there doing nothing.
CHANGED="$(git status --porcelain -- . ':!pipeline/backlog.md' ':!pipeline/state')"
[ -n "$CHANGED" ] || abandon "$AUTHOR changed no files"
log "changed: $(printf '%s' "$CHANGED" | tr '\n' ' ')"

# --- 3. does it work at all ------------------------------------------------

log "gate (fast)"
if ! bash "$PIPE/gate.sh" fast > "$CYCLE_DIR/gate-fast.txt" 2>&1; then
  tail -12 "$CYCLE_DIR/gate-fast.txt" | sed 's/^/    /'
  abandon "the change does not pass the fast gate"
fi
log "gate (fast) passed"

# --- 4. the other one looks at it -----------------------------------------

export FAKE_ROLE=review
log "review by $REVIEWER"
prompt_from review.md "$CYCLE_DIR/review.prompt" \
    "TASK=$TITLE" "REPORT=$(cat "$CYCLE_DIR/author-report.txt")"

if [ "$REVIEWER" = codex ]; then
  run_codex "$CYCLE_DIR/review.prompt" "$CYCLE_DIR/review.json" \
      $(codex_args_for review) > "$CYCLE_DIR/review-stdout.txt" 2>&1
else
  run_claude "$CYCLE_DIR/review.prompt" "$CYCLE_DIR/review.json" $(claude_args_for)
fi

# Reading the reply is fiddly enough to deserve its own file and its own
# tests: braces inside a quoted string, prose around the object, a findings
# field that is not a list. See parse_review.py.
python "$PIPE/parse_review.py" "$CYCLE_DIR/review.json" "$CYCLE_DIR/findings.json"

VERDICT="$(python -c "import json,sys;print(json.load(open(sys.argv[1]))['verdict'])" "$CYCLE_DIR/findings.json")"
N_FIND="$(python -c "import json,sys;print(len(json.load(open(sys.argv[1]))['findings']))" "$CYCLE_DIR/findings.json")"
log "review: $VERDICT, $N_FIND finding(s)"

# --- 5. answer every finding ----------------------------------------------

repairs=0
while [ "$N_FIND" -gt 0 ] && [ "$repairs" -lt "$MAX_REPAIRS" ]; do
  repairs=$((repairs + 1))
  export FAKE_ROLE=address
  log "answering findings (round $repairs)"

  prompt_from address.md "$CYCLE_DIR/address-$repairs.prompt" \
      "TASK=$TITLE" "FINDINGS=$(cat "$CYCLE_DIR/findings.json")"

  if [ "$AUTHOR" = codex ]; then
    run_codex "$CYCLE_DIR/address-$repairs.prompt" "$CYCLE_DIR/address-$repairs.txt" \
        > /dev/null 2>&1
  else
    run_claude "$CYCLE_DIR/address-$repairs.prompt" "$CYCLE_DIR/address-$repairs.txt"
  fi

  log "gate (fast) after repairs"
  if ! bash "$PIPE/gate.sh" fast > "$CYCLE_DIR/gate-fast-$repairs.txt" 2>&1; then
    tail -12 "$CYCLE_DIR/gate-fast-$repairs.txt" | sed 's/^/    /'
    abandon "the repairs broke the fast gate"
  fi

  # Only the serious ones are worth another round trip.
  N_FIND="$(python - "$CYCLE_DIR/findings.json" <<'PY'
import json, sys
f = json.load(open(sys.argv[1]))["findings"]
print(sum(1 for x in f if x.get("severity") == "high"))
PY
)"
  [ "$repairs" -ge 1 ] && break     # one pass; the full gate is the backstop
done

# --- 6. the whole thing, before it goes anywhere --------------------------

log "gate (full)"
if ! bash "$PIPE/gate.sh" full > "$CYCLE_DIR/gate-full.txt" 2>&1; then
  tail -16 "$CYCLE_DIR/gate-full.txt" | sed 's/^/    /'
  abandon "the change does not pass the full gate"
fi
log "gate (full) passed"

# --- landing ---------------------------------------------------------------

git add -A
git commit -q -F - <<EOF
$TITLE

$(head -c 1200 "$CYCLE_DIR/author-report.txt")

Written by $AUTHOR, reviewed by $REVIEWER, $N_FIND finding(s) answered.
Passed the full gate: build, 213 kernel checks, the serial shell test, all
four boot paths, and the three harnesses that drive the desktop.
EOF

git checkout -q main
git merge -q --no-ff "$BRANCH" -m "$TITLE" || abandon "the merge did not apply"
git branch -d "$BRANCH" >/dev/null 2>&1
backlog_set "$TASK_ID" "done"

log "landed on main: $TITLE"

if [ "$PUSH" = 1 ]; then
  git push -q origin main && log "pushed"
else
  log "not pushed (PUSH=1 to push)"
fi
