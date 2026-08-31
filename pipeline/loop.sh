#!/usr/bin/env bash
# Runs cycles until the backlog is empty, or the budget is spent, or
# something goes wrong twice running.
#
#   bash pipeline/loop.sh          work through the backlog
#   MAX_CYCLES=3 bash pipeline/loop.sh
#   PUSH=1 bash pipeline/loop.sh   and push each landed change
#
# The two stopping rules matter more than the loop does. A budget means an
# unattended run cannot spend the afternoon on something hopeless, and
# stopping after two failures in a row means a problem that affects every
# task (a broken toolchain, an agent that has stopped responding) costs two
# cycles instead of the whole backlog.

source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

MAX_CYCLES="${MAX_CYCLES:-6}"
consecutive_failures=0
landed=0
blocked=0

echo "=== pipeline ==="
echo "backlog: $(backlog_count_todo) task(s) to do, budget $MAX_CYCLES cycle(s)"
echo

for ((i = 1; i <= MAX_CYCLES; i++)); do
  todo="$(backlog_count_todo)"
  if [ "$todo" -eq 0 ]; then
    echo "backlog empty"
    break
  fi

  echo "--- cycle $i of $MAX_CYCLES ---"
  if bash "$PIPE/cycle.sh"; then
    landed=$((landed + 1))
    consecutive_failures=0
  else
    blocked=$((blocked + 1))
    consecutive_failures=$((consecutive_failures + 1))
    if [ "$consecutive_failures" -ge 2 ]; then
      echo
      echo "two in a row went wrong; stopping rather than working through the"
      echo "backlog turning every task into a blocked one. The logs are in"
      echo "pipeline/state/."
      break
    fi
  fi
  echo
done

echo "=== done ==="
echo "landed:  $landed"
echo "blocked: $blocked"
echo "left:    $(backlog_count_todo)"
[ "$blocked" -gt 0 ] && echo "blocked tasks keep their logs in pipeline/state/<id>/"
exit 0
