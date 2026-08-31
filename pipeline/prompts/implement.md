# Implementing one task

You are working on nyx, an operating system written entirely from scratch:
its own bootloaders, paging, scheduler, FAT16 driver, TCP stack, window
manager and font. Read `README.md` first if you have not already.

## The task

    {{TASK}}

## What matters here

**Nothing third party.** Every line is written for this project. The only
exception is the .NET runtime the Windows launcher needs. Do not add a
library, a package, or a vendored file. If a task seems to need one, it does
not: write the thing.

**It has to actually work.** This project's bugs have almost all been found
by running it, not by reading it. The kernel has a self test with 213 checks,
a serial shell test, a four-way boot test, and three harnesses that drive the
desktop and the keyboard through QEMU's monitor. Add to them. A change with
no way to tell whether it worked is not finished.

**A test that cannot fail proves nothing.** When you add one, break the thing
it tests on purpose, watch the test fail, then put it back. Say in your
report that you did this and what happened. This has caught several checks
here that were passing on nothing at all.

**Write like the code around it.** Read a neighbouring file before you start.
Comments explain why a thing is the way it is, not what the line does. No
em-dashes. Plain words. If a comment could be deleted without losing
anything, delete it.

**Never claim something passed without running it.** If you could not run
something, say so plainly.

## How to check your work

    bash pipeline/gate.sh fast

That builds, runs the kernel's 213 checks, and drives the shell over the
serial line. It takes about five minutes. Run it before you finish. If it
fails, fix it; if you cannot, say exactly what failed and why.

## When you are done

Leave the working tree with your changes in it, uncommitted. Do not commit,
do not branch, do not push: the pipeline does that once your work has been
checked.

Then write a short report as your final message:

- what you changed and why
- what you ran, and what it said
- anything you deliberately did not do, and why
- anything you are unsure about that the reviewer should look hard at

Be honest in that last one. The reviewer is another model with no stake in
your work, and pointing it at your weakest spot is the fastest way to a
correct result.
