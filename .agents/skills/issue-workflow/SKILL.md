---
name: issue-workflow
description: Use when creating, updating, handing off, or closing a GitHub issue for amble-home agent work.
---

# Issue workflow

- GitHub issues on `clwill/amble-home` are the work record: specs under review, acceptance evidence, handoffs, decisions with revisit triggers. Product truth lives in the repo docs; the issue is the record of how it got there.
- Every agent-authored issue carries its author label (`claude` or `codex`), one area label and the kind labels that apply (vocabulary in root `AGENTS.md`; `gh label list` before inventing one). `operator-action` marks anything only Chris can do: buy hardware, flash the production bridge, approve a spec, decide scope.
- Body: problem, evidence, acceptance criteria, non-goals, risk boundaries (does it touch production, keys, or the release pipeline), intended driver.
- Write for Chris, who does not read the code. Lead with the decision or outcome he needs; put file paths and log excerpts after.
- `unattended` is Chris-applied only; his comment carries the ceiling and, optionally, a pre-selected review path. It expires with the issue and never authorizes adjacent scope, a push or a release.
- A cross-model review handoff is a comment on the existing issue in the `review-work-package` envelope shape; the reviewer answers on the same issue. No new issue for review.
- Implementation comments record the committed `base..head` range, checks run, hardware evidence, review lane and verdict, repaired defect ids and withheld steps (push, release, production flash).
- Close only when acceptance is met. `Closes #N` in a commit message only when the commit should actually close it.
