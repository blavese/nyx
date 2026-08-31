#!/usr/bin/env bash
# Checks the pipeline itself, with scripted agents instead of real ones.
#
# The parts of cycle.sh most likely to be wrong are not about code: they are
# about order and state. Does a failed gate really delete the branch? Does a
# blocked task really go back in the backlog as blocked? Does a review with
# findings really come back to the author? Running real agents to find that
# out costs an hour a go, so this runs the same script against canned replies
# and a stubbed gate, in seconds.
#
# It does not check that the agents are any good. It checks that the machine
# around them does what it says.
#
#   bash pipeline/selftest.sh

set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
ROOT="$PWD"

FAKES="$(mktemp -d)"
SANDBOX="$(mktemp -d)"
trap 'rm -rf "$FAKES" "$SANDBOX"' EXIT

pass=0; fail=0
check() {
  if [ "$2" = "$3" ]; then printf '  PASS  %s\n' "$1"; pass=$((pass+1))
  else printf '  FAIL  %s (wanted %s, got %s)\n' "$1" "$3" "$2"; fail=$((fail+1)); fi
}

# --- a repository to play in ----------------------------------------------
#
# A copy, not the real one: a test that can leave your work on a branch
# somewhere is not one you would run twice.
setup_repo() {
  rm -rf "$SANDBOX/repo"
  mkdir -p "$SANDBOX/repo/pipeline/prompts"
  cd "$SANDBOX/repo"
  git init -q .
  git config user.email t@t; git config user.name t

  cp "$ROOT/pipeline/lib.sh" "$ROOT/pipeline/cycle.sh" pipeline/
  cp "$ROOT/pipeline"/prompts/*.md pipeline/prompts/
  echo "placeholder" > thing.c

  # A stub gate, so this test is about the pipeline rather than about QEMU.
  # GATE_RESULT decides what it says.
  cat > pipeline/gate.sh <<'STUB'
#!/usr/bin/env bash
echo "stub gate ($1): ${GATE_RESULT:-0}"
exit "${GATE_RESULT:-0}"
STUB

  cat > pipeline/backlog.md <<'BL'
| id | author | title | state |
|---|---|---|---|
| demo | claude | A demonstration task | todo |
BL

  git add -A && git commit -qm init
  git branch -M main
}

# Cleared between scenarios. Leaving the previous one's canned edit lying
# around is how a test that should watch an agent do nothing watches it do
# the last test's work instead.
reset_canned() { rm -f "$FAKES"/*.txt "$FAKES"/*.sh 2>/dev/null; return 0; }

canned() {                       # canned <role> <reply> [shell to run]
  printf '%s\n' "$2" > "$FAKES/$1.txt"
  if [ -n "${3:-}" ]; then printf '%s\n' "$3" > "$FAKES/$1.sh"; fi
}

state_of() { awk -F'|' '/^\| *demo *\|/ { s=$5; gsub(/^ +| +$/,"",s); print s }' pipeline/backlog.md; }

echo "=== pipeline self test ==="

# --- 1. the happy path ----------------------------------------------------
setup_repo
reset_canned
canned author "changed a thing" 'echo "// author was here" >> thing.c'
canned review '{"verdict":"sound","findings":[]}'
FAKE_AGENTS="$FAKES" GATE_RESULT=0 bash pipeline/cycle.sh >"$SANDBOX/out1.txt" 2>&1
rc=$?
check "a clean task lands"                "$rc" "0"
check "and is marked done"                "$(state_of)" "done"
check "on main, not a branch"             "$(git rev-parse --abbrev-ref HEAD)" "main"
check "the branch is gone"                "$(git branch --list 'pipeline/demo' | wc -l | tr -d ' ')" "0"
check "the author's change is on main"    "$(git show main:thing.c | grep -c 'author was here')" "1"

# --- 2. the gate says no --------------------------------------------------
#
# The whole point of the thing. A change that does not pass must not land,
# whatever either agent said about it.
setup_repo
reset_canned
canned author "changed a thing" 'echo "// broken" >> thing.c'
canned review '{"verdict":"sound","findings":[]}'
FAKE_AGENTS="$FAKES" GATE_RESULT=1 bash pipeline/cycle.sh >"$SANDBOX/out2.txt" 2>&1
rc=$?
check "a failing gate stops the task"     "$rc" "1"
check "and it is marked blocked"          "$(state_of)" "blocked"
check "nothing landed on main"            "$(git log --oneline | wc -l | tr -d ' ')" "1"
check "the branch is cleaned up"          "$(git branch --list 'pipeline/demo' | wc -l | tr -d ' ')" "0"
check "the source tree is left clean"     "$(git status --porcelain -- . ':!pipeline' | wc -l | tr -d ' ')" "0"

# --- 3. an agent that does nothing ----------------------------------------
setup_repo
reset_canned
canned author "I had a look and decided not to"      # no shell: no edits
canned review '{"verdict":"sound","findings":[]}'
FAKE_AGENTS="$FAKES" GATE_RESULT=0 bash pipeline/cycle.sh >"$SANDBOX/out3.txt" 2>&1
rc=$?
check "an author that changes nothing stops" "$rc" "1"
check "and the task is blocked"              "$(state_of)" "blocked"

# --- 4. findings come back to the author ----------------------------------
setup_repo
reset_canned
canned author "changed a thing" 'echo "// v1" >> thing.c'
canned review '{"verdict":"needs-work","findings":[{"severity":"high","where":"thing.c:1","what":"wrong","why":"because","fix":"do it again"}]}'
canned address "fixed it" 'echo "// v2 after review" >> thing.c'
FAKE_AGENTS="$FAKES" GATE_RESULT=0 bash pipeline/cycle.sh >"$SANDBOX/out4.txt" 2>&1
rc=$?
check "a review with findings still lands when answered" "$rc" "0"
check "the answer is in the commit" \
      "$(git show HEAD:thing.c | grep -c 'v2 after review')" "1"
check "the round trip happened" \
      "$(grep -c 'answering findings' "$SANDBOX/out4.txt")" "1"

# --- 5. a review that is not JSON -----------------------------------------
#
# Models wrap things in prose. That must not read as "no findings".
setup_repo
reset_canned
canned author "changed a thing" 'echo "// v1" >> thing.c'
canned review 'I could not produce JSON, sorry.'
FAKE_AGENTS="$FAKES" GATE_RESULT=0 bash pipeline/cycle.sh >"$SANDBOX/out5.txt" 2>&1
check "an unreadable review is not silently a pass" \
      "$([ "$(grep -c 'unreadable' "$SANDBOX/out5.txt")" -ge 1 ] && echo yes || echo no)" "yes"

# --- 6. JSON wrapped in prose is still read -------------------------------
setup_repo
reset_canned
canned author "changed a thing" 'echo "// v1" >> thing.c'
canned review 'Here is my review:
{"verdict":"sound","findings":[]}
Hope that helps.'
FAKE_AGENTS="$FAKES" GATE_RESULT=0 bash pipeline/cycle.sh >"$SANDBOX/out6.txt" 2>&1
check "JSON inside prose is still read"   "$(state_of)" "done"

cd "$ROOT"
echo
if [ "$fail" -eq 0 ]; then echo "pipeline self test: all $pass checks passed"; exit 0; fi
echo "pipeline self test: $fail failed"
exit 1
