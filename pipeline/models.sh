#!/usr/bin/env bash
# Which model does which job, and how much it costs to run.
#
# The pipeline spends from two separate buckets: a Claude subscription and a
# Codex one. Wall clock is nearly free here, because most of a cycle is QEMU
# and QEMU costs nothing. Agent turns are the expensive part, and running
# three tasks at once does not reduce them, it just spends them faster.
#
# So the knobs that matter are which model, how hard it thinks, and how many
# agent runs a task takes. Pick a profile:
#
#   PROFILE=thrifty    codex writes and codex reviews, in separate sessions.
#                      Spends nothing from the Claude bucket at all. The
#                      reviewer is a different session rather than a
#                      different model, which is weaker but not nothing:
#                      it still has no stake in the code and has not seen
#                      the author's reasoning.
#
#   PROFILE=balanced   codex writes, claude reviews on Sonnet. Keeps the
#                      cross-model check, which is where the real bugs have
#                      come from, and reviewing is the cheap half: it reads
#                      a diff and answers in JSON rather than exploring a
#                      repository. This is the default.
#
#   PROFILE=max        opus writes, sol reviews at high effort, both
#                      directions. What to use when a task is genuinely
#                      hard and you do not mind paying for it.
#
#   bash pipeline/batch.sh                      balanced, one task
#   PROFILE=thrifty bash pipeline/batch.sh      no claude usage
#   PROFILE=max BATCH=2 bash pipeline/batch.sh  when it matters

PROFILE="${PROFILE:-balanced}"

case "$PROFILE" in
thrifty)
  # Everything on the codex bucket. Luna is the cheap one; the gate is
  # doing most of the work of catching mistakes anyway.
  : "${CLAUDE_MODEL:=sonnet}"
  : "${CODEX_MODEL:=gpt-5.6-luna}"
  : "${CODEX_EFFORT_IMPLEMENT:=medium}"
  : "${CODEX_EFFORT_REVIEW:=medium}"
  : "${CODEX_EFFORT_ADDRESS:=low}"
  # Both roles go to codex regardless of what the backlog says.
  FORCE_AUTHOR=codex
  FORCE_REVIEWER=codex
  ;;
max)
  : "${CLAUDE_MODEL:=opus}"
  : "${CODEX_MODEL:=gpt-5.6-sol}"
  : "${CODEX_EFFORT_IMPLEMENT:=high}"
  : "${CODEX_EFFORT_REVIEW:=high}"
  : "${CODEX_EFFORT_ADDRESS:=medium}"
  FORCE_AUTHOR=""
  FORCE_REVIEWER=""
  ;;
*)
  # Writing is the expensive half, so it goes on the codex bucket; reading
  # a diff is the cheap half, so the second opinion stays a real second
  # model rather than a second session of the first.
  : "${CLAUDE_MODEL:=sonnet}"
  : "${CODEX_MODEL:=gpt-5.6-sol}"
  : "${CODEX_EFFORT_IMPLEMENT:=medium}"
  : "${CODEX_EFFORT_REVIEW:=medium}"
  : "${CODEX_EFFORT_ADDRESS:=low}"
  FORCE_AUTHOR=codex
  FORCE_REVIEWER=claude
  ;;
esac

# Whether to spend a third agent run answering the review. Off means the
# findings are written down and the task lands or does not on the gate
# alone, which saves a run per task at the cost of ignoring medium ones.
ANSWER_FINDINGS="${ANSWER_FINDINGS:-1}"

codex_args_for() {
  case "$1" in
    review)  printf -- '-m %s -c model_reasoning_effort="%s"' "$CODEX_MODEL" "$CODEX_EFFORT_REVIEW" ;;
    address) printf -- '-m %s -c model_reasoning_effort="%s"' "$CODEX_MODEL" "$CODEX_EFFORT_ADDRESS" ;;
    *)       printf -- '-m %s -c model_reasoning_effort="%s"' "$CODEX_MODEL" "$CODEX_EFFORT_IMPLEMENT" ;;
  esac
}

claude_args_for() { printf -- '--model %s' "$CLAUDE_MODEL"; }

# The backlog names an author per task; a profile may override it.
author_for()   { [ -n "$FORCE_AUTHOR" ]   && printf '%s' "$FORCE_AUTHOR"   || printf '%s' "$1"; }
reviewer_for() {
  [ -n "$FORCE_REVIEWER" ] && { printf '%s' "$FORCE_REVIEWER"; return; }
  [ "$1" = claude ] && printf 'codex' || printf 'claude'
}
