# Answering a review

Another model reviewed your change to nyx. It did not write the code and has
no stake in it.

## What you were doing

    {{TASK}}

## What it found

    {{FINDINGS}}

## What to do

Take each finding in turn. There are only two honest outcomes:

**Fix it.** If it is right, fix it properly rather than papering over the
symptom. Where the finding is about something that goes wrong in a specific
case, add a check that would have caught it, and break the code on purpose
once to confirm the check actually fails.

**Reject it, with a reason.** A reviewer working from a diff does not have
the whole picture and can be wrong. If it is wrong, say exactly why: the
invariant it missed, the caller that cannot do that, the guard further up.
"I disagree" is not a reason. Being right and saying why is a perfectly good
outcome and is not a failure.

Do not quietly skip one. Every finding gets an outcome.

## Then

Run the gate again:

    bash pipeline/gate.sh fast

Leave your changes uncommitted in the working tree.

Finish with a short report: one line per finding saying fixed or rejected and
why, then what the gate said.
