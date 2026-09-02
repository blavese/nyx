#!/usr/bin/env bash
# Which model does which job.
#
# The two sides are matched deliberately. Claude runs on Opus, the strongest
# of its family, so its counterpart is Codex's strongest rather than the
# everyday one: a review by something weaker than the thing that wrote the
# code is worth very little, and that is the step this whole pipeline exists
# for.
#
# Codex's models, from `~/.codex/models_cache.json`:
#
#   gpt-5.6-sol     the workhorse, listed first, goes up to "ultra" effort
#   gpt-5.6-terra   balanced coding model
#   gpt-5.6-luna    fast and cheap
#   gpt-5.4-mini    small and cheap
#
# Effort is separate from the model and matters as much. Sol at low is what
# the desktop app defaults to and is not what you want writing a page table
# walker.
#
# Override any of these from the environment to spend less:
#
#   CODEX_MODEL=gpt-5.6-luna bash pipeline/batch.sh

# The model each side uses, by role.
CLAUDE_MODEL="${CLAUDE_MODEL:-opus}"
CODEX_MODEL="${CODEX_MODEL:-gpt-5.6-sol}"

# Reasoning effort for codex, by role. Writing a driver and finding what is
# wrong with one are both hard; answering a specific finding is not, because
# the thinking has already been done and written down.
CODEX_EFFORT_IMPLEMENT="${CODEX_EFFORT_IMPLEMENT:-high}"
CODEX_EFFORT_REVIEW="${CODEX_EFFORT_REVIEW:-high}"
CODEX_EFFORT_ADDRESS="${CODEX_EFFORT_ADDRESS:-medium}"

# Returns the codex arguments for a role.
codex_args_for() {
  case "$1" in
    review)  printf -- '-m %s -c model_reasoning_effort="%s"' "$CODEX_MODEL" "$CODEX_EFFORT_REVIEW" ;;
    address) printf -- '-m %s -c model_reasoning_effort="%s"' "$CODEX_MODEL" "$CODEX_EFFORT_ADDRESS" ;;
    *)       printf -- '-m %s -c model_reasoning_effort="%s"' "$CODEX_MODEL" "$CODEX_EFFORT_IMPLEMENT" ;;
  esac
}

claude_args_for() { printf -- '--model %s' "$CLAUDE_MODEL"; }
