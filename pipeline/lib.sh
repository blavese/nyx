#!/usr/bin/env bash
# Shared pieces: finding the two agents, running them, and writing things down.
#
# Both agents are command line programs that take a prompt and edit files in
# place. That is the whole reason this works: neither of them needs a person
# sitting there, so a script can hand work to one, hand the result to the
# other, and keep going.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PIPE="$ROOT/pipeline"
STATE="$PIPE/state"
BACKLOG="$PIPE/backlog.md"

mkdir -p "$STATE"

# --- finding codex ---------------------------------------------------------
#
# The installer puts the binary under a directory named after a build hash,
# so it moves on every update. Look it up rather than writing it down.
find_codex() {
  if command -v codex >/dev/null 2>&1; then command -v codex; return; fi
  local base="$LOCALAPPDATA_UNIX/OpenAI/Codex/bin"
  [ -d "$base" ] || base="/c/Users/$USER/AppData/Local/OpenAI/Codex/bin"
  find "$base" -name 'codex.exe' -type f 2>/dev/null | head -1
}

LOCALAPPDATA_UNIX="${LOCALAPPDATA:-/c/Users/$USER/AppData/Local}"
LOCALAPPDATA_UNIX="$(printf '%s' "$LOCALAPPDATA_UNIX" | sed 's|\\|/|g; s|^\([A-Za-z]\):|/\L\1|')"

CODEX="${CODEX:-$(find_codex)}"
CLAUDE="${CLAUDE:-$(command -v claude || echo "$HOME/.local/bin/claude")}"

# --- saying what is happening ----------------------------------------------

log()  { printf '%s  %s\n' "$(date +%H:%M:%S)" "$*" | tee -a "${CYCLE_LOG:-/dev/null}"; }
warn() { printf '%s  !! %s\n' "$(date +%H:%M:%S)" "$*" | tee -a "${CYCLE_LOG:-/dev/null}" >&2; }

die() { warn "$*"; exit 1; }

# --- running the agents ----------------------------------------------------
#
# Both are given the repository as their working directory and allowed to
# write in it. Neither is allowed to decide whether its own work was any
# good: that is what gate.sh is for, and it is not something either of them
# can talk its way past.

# A seam for testing the pipeline itself. With FAKE_AGENTS pointing at a
# directory of canned replies, cycle.sh runs end to end in seconds against
# scripted agents, which is the only practical way to check the parts that
# are about order and state rather than about code. See selftest.sh.
fake_agent() {
  local role="$1" out="$2"
  local canned="$FAKE_AGENTS/$role"
  [ -f "$canned.txt" ] || { warn "no canned reply for $role"; return 1; }
  cp "$canned.txt" "$out"
  # Tested with -f rather than -x: the executable bit does not survive on a
  # Windows filesystem, so -x is false for a file that was just chmod'd and
  # the canned edit silently never happens.
  [ -f "$canned.sh" ] && ( cd "$ROOT" && bash "$canned.sh" )
  return 0
}

# run_codex <prompt-file> <output-file> [extra args...]
run_codex() {
  local prompt_file="$1" out="$2"; shift 2
  [ -n "${FAKE_AGENTS:-}" ] && { fake_agent "$FAKE_ROLE" "$out"; return $?; }
  [ -x "$CODEX" ] || { warn "codex not found"; return 127; }

  timeout "${AGENT_TIMEOUT:-3600}" "$CODEX" exec \
    --cd "$ROOT" \
    --sandbox workspace-write \
    --skip-git-repo-check \
    -o "$out" \
    "$@" \
    - < "$prompt_file"
}

# run_claude <prompt-file> <output-file>
run_claude() {
  local prompt_file="$1" out="$2"; shift 2
  [ -n "${FAKE_AGENTS:-}" ] && { fake_agent "$FAKE_ROLE" "$out"; return $?; }
  [ -x "$CLAUDE" ] || command -v claude >/dev/null || { warn "claude not found"; return 127; }

  # The tools it is allowed, named rather than blanket-bypassed. Bash is
  # the one that matters: without it the agent writes code and then asks
  # permission to build it, which unattended means it never builds at all.
  # Nothing here reaches the network.
  #
  # The prompt goes on stdin because --allowedTools takes a list and would
  # otherwise swallow a prompt given as an argument.
  ( cd "$ROOT" && timeout "${AGENT_TIMEOUT:-3600}" "$CLAUDE" -p \
      --permission-mode acceptEdits \
      --allowedTools "Bash Read Write Edit Glob Grep" \
      --add-dir "$ROOT" \
      "$@" \
      < "$prompt_file" ) > "$out" 2>&1
}

# --- the backlog -----------------------------------------------------------
#
# A markdown table, because both agents read it and so does a person. The
# state column is the only thing the scripts write.

# backlog_next -> "id|author|title" of the first task marked todo
backlog_next() {
  awk -F'|' '
    /^\| *[a-z0-9-]+ *\|/ {
      gsub(/^ +| +$/, "", $2); gsub(/^ +| +$/, "", $3)
      gsub(/^ +| +$/, "", $4); gsub(/^ +| +$/, "", $5)
      if ($5 == "todo") { print $2 "|" $3 "|" $4; exit }
    }' "$BACKLOG"
}

# backlog_set <id> <state>
#
# Rebuilds the row rather than editing the state cell in place. A markdown
# table does not care about column widths, and trying to preserve them is how
# a rewrite ends up mangling the row it was meant to leave alone.
backlog_set() {
  local id="$1" state="$2" tmp
  tmp="$(mktemp)"
  awk -F'|' -v id="$id" -v st="$state" '
    {
      if ($0 ~ /^\| *[a-z0-9-]+ *\|/) {
        want = $2; gsub(/^ +| +$/, "", want)
        if (want == id) {
          a = $3; gsub(/^ +| +$/, "", a)
          t = $4; gsub(/^ +| +$/, "", t)
          print "| " want " | " a " | " t " | " st " |"
          next
        }
      }
      print
    }' "$BACKLOG" > "$tmp" && mv "$tmp" "$BACKLOG"
}

backlog_count_todo() {
  awk -F'|' '/^\| *[a-z0-9-]+ *\|/ { s=$5; gsub(/^ +| +$/, "", s); if (s=="todo") n++ } END { print n+0 }' "$BACKLOG"
}
