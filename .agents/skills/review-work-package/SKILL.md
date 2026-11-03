---
name: review-work-package
description: Use when a substantive amble-home slice is ready for review, when classifying its review lane, when posting or answering a cross-model review handoff, when acting as a read-only reviewer, or when repairing review defects.
---

# Review a work package

## Core rule

Every substantive code-bearing slice receives review of one clean, committed `base..head` range. The driver stays the only writer; the reviewer is fresh, isolated and read-only. Routine same-vendor review needs no approval from Chris. An escalated lane means cross-model review, which the driver runs through the Codex CLI bridge by default (standing authorization); Chris may pick the GitHub handoff or a waiver per issue.

Review is an explicit driver action. A commit, test, stop, idle period, hook, watcher or schedule never starts review. Copy-only, documentation, formatting and obvious mechanical edits are exempt.

## Choose the lane

| Lane | Use when | Reviewer |
|---|---|---|
| Routine | All five routine criteria hold | Fresh same-vendor reviewer |
| Escalated | Any trigger in root `AGENTS.md` applies, or the lane is ambiguous | Chris chooses the path |

Routine requires all five: (1) established local pattern; (2) deterministic acceptance evidence, which for mesh changes means hardware evidence on the dev board; (3) loud, recoverable failure; (4) no changed safety invariant, persisted-data behavior or shared-contract semantics (HA entity contract, NVS layout, importer payload); (5) no unresolved architectural judgment.

Record the lane and a one-sentence rationale in the request. A routine reviewer that finds a trigger returns `escalation_required` and stops.

## Escalated: the Codex CLI bridge by default, or Chris's per-issue choice

1. **Codex CLI bridge (default).** The driver writes the envelope below to a prompt file that also says "load `.agents/skills/review-work-package/SKILL.md` first; you are the reviewer", then runs from the repo root, in the background: `codex exec --sandbox read-only --color never -c model_reasoning_effort='"high"' -o <result.md> - < <prompt.md>`. Never `--ephemeral`; capture the `session id`. Targeted rounds: `codex exec --sandbox read-only -o <out> resume <session-id> - < <prompt>`. The sandbox has no network, so paste issue text into the envelope.
2. **GitHub handoff.** Post the envelope as a comment on the existing issue and stop writing. Chris starts the other vendor; findings and verdict live on that issue.
3. **Waived.** Record the decision once, run routine review and normal verification, do not reopen.

A waiver never resolves correctness, scope or safety ambiguity in the work itself: stop and ask Chris.

## The envelope (driver posts)

- Issue numbers; `broad` or `targeted`.
- Full `base` and `head` SHAs; whether pushed. `base` must be an ancestor of `head`, tree clean.
- Objective, acceptance criteria, explicit non-goals.
- Lane and one-sentence rationale.
- Authorities: `AGENTS.md`, the spec, decisions entries, `docs/origin.md` where the mesh layer is involved.
- Exact verification commands and observed results, including hardware evidence.
- Actions intentionally withheld (push, release, production flash).
- For `targeted`: the original numbered findings.

Never include where the driver suspects defects.

## The result (reviewer posts)

1. **Verdict:** `pass`, `defects` or `escalation_required`.
2. **Defects:** `D1`, `D2`, … each with path/line evidence, the violated criterion or invariant, user impact, and the smallest adequate correction.
3. **Suggestions:** separate, non-blocking.
4. **Lane challenge:** absent, or the concrete trigger and reason.

A defect is unmet requested behavior or acceptance, or a change that permits silent wrongness, key leakage, a bricked or unrecoverable device, a command reaching production fixtures from the dev build, or a destructive path. Verify a finding's premise against the repo before reporting it. Reviewers never edit, commit, push, file issues or fix.

## Bounded convergence

1. One broad review. 2. `pass` completes it; `escalation_required` returns to Chris. 3. For `defects`: repair, or counter with evidence, or take a dispute to Chris. 4. Rerun verification, commit, ask the **same** reviewer for targeted verification of the numbered defects. 5. One more repair and one final targeted round. Anything left goes to Chris.

## Record

The issue records: range, lane and rationale, Chris's path when escalated, reviewer vendor, verdict, repaired defect ids, final verification. Review never authorizes a push, release or production flash.
