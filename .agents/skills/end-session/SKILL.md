---
name: end-session
description: Use when Chris asks to end, wrap up, close out, or hand off an amble-home work session.
---

# End a session

1. Inspect branch, status, full diff, recent commits and unpushed state (`origin/main..HEAD` covers every unpushed commit, not only this session's). Confirm whether the work is driver-only or awaiting independent review.
2. Apply the push gate from root `AGENTS.md`: full-diff inspection plus proportional verification via `verify-surfaces`. Do not manufacture a review requirement Chris waived.
3. Reconcile current-state instructions. Update `AGENTS.md` only for lasting operating truth (rewrite the Status paragraph in place; it should usually shrink), `docs/decisions.md` for durable rationale with alternatives rejected, `docs/changelog.md` for build history. Keep narrative out of `AGENTS.md`.
4. Apply `review-work-package`: a substantive code-bearing slice gets routine same-vendor review of its committed range; if an escalation trigger applies, name it and take Chris's issue-scoped path. Record lane, range, reviewer vendor, verdict and repaired defect ids on the issue.
5. Commit and push only when authorized by Chris. Never cut a release or flash the production bridge as part of wrap-up.
6. Update every GitHub issue touched: what changed, how it was verified (hardware evidence for mesh changes), commit hashes, what is next. Close only when acceptance is met. Open issues for anything new that outlives the session, with area + kind labels and the author label.
7. Refresh Claude memory for this project if a durable fact, preference or correction surfaced that the repo does not already record.
8. Summarize for Chris in plain language: what shipped, what is verified on hardware, what needs his hands (purchases, flashing production, decisions), and the next issue.
