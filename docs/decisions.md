# Decisions

## 2026-11-09: Only the private-check from the repo standard

This repo is not on the owner's repo standard (see "Standards debt" in `AGENTS.md`). Chose to add
only the two `private-check` workflows, `scripts/standard/private_check.py` and a `make
private-check` target, copied unchanged from repo-standard v0.17.0's public render; rejected
adopting the whole standard here because the repo was deliberately left as built, and this check
is needed in every public repo now. Update those three files by copying a later render over them;
they are not edited in this repo.

`private-check` is required on `main`. `private-check-fork` was not required at first (owner's
decision, 2026-11-09); the entry below reverses that.

## 2026-11-13: `private-check-fork` is a required check

Chose to require `private-check-fork` on `main` beside `private-check` (owner's decision,
2026-11-13); rejected leaving it optional because on a fork's pull request `private-check` gets
no secret and skips, GitHub counts a skipped required check as a pass, and so the only real scan
of a fork's pull request is `private-check-fork`. On the repo's own pull requests it skips, so
requiring it costs nothing there.
