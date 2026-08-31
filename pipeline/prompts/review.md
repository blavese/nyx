# Reviewing one change

You are reviewing a change to nyx, an operating system written entirely from
scratch. You did not write this change. That is the point: the author's own
tests already passed, so anything left is what those tests do not look at.

## What was being attempted

    {{TASK}}

## What the author said they did

    {{REPORT}}

## The change

Run `git diff` to see it. The working tree holds the change, uncommitted.

## What to look for

The author has already run the build, the kernel's 213 self checks and the
serial shell test, and they passed. So do not spend your time there. Spend it
where those cannot reach:

**Memory that outlives its owner.** This kernel hands raw pointers between
tasks. Every real bug found here so far has been of this shape: a window
surface freed while a program was still mapped to it, a page unmapped in the
wrong address space, a task record reaped while something still held it. Ask
who owns each allocation and when it goes.

**Two tasks touching one thing.** The window manager runs in one task and
programs run in others, with no locks. If this change lets both reach the
same structure, say so and say what happens if the switch lands in the middle.

**Assumptions that only hold on QEMU.** Firmware differs. Memory is not
zeroed. A device may report something unexpected. Several bugs here only
appeared on a stricter machine.

**Arithmetic that is fine until it is not.** Sizes that fit today, a count
that fits in sixteen bits, a loop bound derived from something a caller
controls.

**Tests that cannot fail.** If the change adds a check, would it fail if the
thing it tests were broken? Say so if not.

## What not to do

Do not restyle code. Do not suggest a library. Do not report something you
have not convinced yourself is real: a long list of maybes is worse than
three things that are true, because it buries them.

If the change is sound, say so and stop. That is a useful answer.

## Your answer

Reply with JSON and nothing else:

    {
      "verdict": "sound" | "needs-work",
      "findings": [
        {
          "severity": "high" | "medium" | "low",
          "where": "file.c:123",
          "what": "one sentence on what is wrong",
          "why": "the specific case where it goes wrong: inputs, ordering, state",
          "fix": "what to do about it"
        }
      ]
    }

`high` means it can corrupt memory, lose data, hang, or crash. `medium` means
it is wrong in a case that will happen. `low` is everything else. An empty
findings list with verdict "sound" is a perfectly good review.
