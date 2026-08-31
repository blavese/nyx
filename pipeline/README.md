# The pipeline

Two models working on nyx without a person in the loop, and a gate neither
of them can talk its way past.

## Why it is shaped like this

Both Claude Code and Codex can be run from a shell script: given a prompt and
a directory, each edits files in place and prints a report. So a script can
hand work to one, hand the result to the other, and keep going.

The interesting question is what they should each do. Having both write
features is the obvious answer and the wrong one: they collide in the same
files and duplicate each other's thinking. What has actually worked on this
project is one writing and the *other* reading. Every serious bug found here
so far was found by someone other than the author, or by a test failing, and
never by the author re-reading their own code. Codex found a double free in
`wm_close`, a permission check that accepted merely-present pages, and a
quadratic allocator in the FAT driver, all in code whose own tests passed.

So: one model writes, the other reviews, and they swap by task.

The other half is that neither of them decides whether the work was any
good. `gate.sh` does, mechanically, and nothing reaches `main` without it.
An agent reporting success is not evidence.

## Before the first run

Both agents have to be signed in, and they sign in separately from whatever
session you are reading this in.

    bash pipeline/preflight.sh

That asks each one a trivial question and checks the emulator, python, and
the state of the tree. It takes a few seconds and it exists because the
expensive failure is not a task going wrong, it is a task going wrong for a
reason that would have stopped every task. An expired login turns the whole
backlog into blocked tasks one cycle at a time.

If it says claude did not answer, run `claude` once, sign in, and quit. The
command line tool keeps its own credentials.

`loop.sh` runs preflight first and refuses to start if it fails.

## Running it

    bash pipeline/loop.sh                 work through the backlog
    MAX_CYCLES=3 bash pipeline/loop.sh    at most three tasks
    PUSH=1 bash pipeline/loop.sh          and push each one that lands
    bash pipeline/cycle.sh                exactly one task

`PUSH` is off by default. Landing on local `main` is reversible; pushing to a
public repository is less so, and the difference should be a decision rather
than an accident.

## What one cycle does

1. Takes the top `todo` task from `backlog.md` and makes a branch for it.
2. The task's `author` writes the code.
3. `gate.sh fast` runs: build, the kernel's 213 self checks, the serial shell
   test. About five minutes. If it fails, the branch is deleted and the task
   is marked `blocked` with its logs kept.
4. The *other* model reviews the diff and answers in JSON: sound, or a list
   of findings with severities.
5. The author answers every finding, fixing it or refusing with a reason.
   Nothing is silently dropped.
6. `gate.sh full` runs: everything above plus all four boot paths and the
   three harnesses that drive the desktop and the keyboard through QEMU's
   monitor. About thirty-five minutes.
7. Only then does it commit and merge to `main`.

A task that fails at any gate never reaches `main`. Its branch is deleted and
`pipeline/state/<id>/` keeps the prompts, both agents' reports, the review
findings and the gate output, so it can be picked up by hand.

## The gate

    bash pipeline/gate.sh fast
    bash pipeline/gate.sh full

This is the only thing in the pipeline that decides anything, so it is worth
being suspicious of. It has been checked three ways: with a deliberately
failing kernel check (fails, exit 1), with a deliberate syntax error (fails,
and stops rather than testing the previous build), and with the tree in a
known good state (passes).

If the build fails it stops there. `build/nyx.bin` is whatever the last
successful build left behind, so carrying on would say something true about
code that no longer exists.

## The backlog

`backlog.md` is a markdown table. The scripts only ever write the `state`
column; everything else is for people to read and edit. Add tasks, reorder
them, change who writes them.

Keep them small. "Add a driver" is a task. "Finish the operating system" is
not, and an agent given that will produce something shaped like an answer
rather than an answer.

A `blocked` task needs a person: read the log, work out whether the task was
wrong or the attempt was, then either fix the task and set it back to `todo`
or delete it.

## What this will not do

**It will not know when a task was a bad idea.** The gate proves a change
builds, boots and passes its tests. It cannot tell you the feature was not
worth having, or that it is the wrong shape, or that two tasks should have
been one. That judgement stays with you, and the place to exercise it is the
backlog.

**It will not catch what nothing tests.** The gate is strong where this
project has tests and blind where it does not. A change to something with no
harness gets past on the reviewer's reading alone. When you add a subsystem,
add a way to check it, or the pipeline quietly gets weaker as the project
grows.

**It will not stop two agents disagreeing forever.** The author gets one pass
to answer the reviewer; then the full gate decides. If they are going back
and forth on something real, the task is probably wrong and wants a person.

**It is not free.** Every cycle spends tokens on both accounts and about
forty minutes of wall clock, most of it in QEMU.

## Files

    preflight.sh        checks both agents answer before spending an hour
    lib.sh              finding the agents, running them, reading the backlog
    gate.sh             the verification, fast and full
    cycle.sh            one task, start to finish
    loop.sh             cycles, with a budget and a stop rule
    backlog.md          what to do next
    prompts/
      implement.md      writing a change
      review.md         reading someone else's
      address.md        answering a review
    selftest.sh         the pipeline's own test, with scripted agents
    state/<task-id>/    everything a cycle produced, kept whether it worked
