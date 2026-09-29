# Skill Observation Log

Observations captured during task-oriented work. Each entry identifies a
potential skill improvement or new skill opportunity.

**Status key:** OPEN = not yet actioned | ACTIONED = skill updated/created |
DECLINED = user decided not to pursue

---

## 2026-09-29 — Graphics project visual overhaul

### Observation 1: Judge realism from the viewpoint the user will look from
**Status:** OPEN

**Date:** 2026-09-29
**Session context:** Upgrading a real-time OpenGL scene to a "AAA open world" look; progress was judged from high overview screenshots.
**Skill:** New skill candidate: realtime-graphics-art-pass
**Type:** open-source
**Phase/Area:** Verification captures

**Issue:** The agent evaluated its visual upgrades mostly from elevated overview shots, where the ground looked acceptable. The user then said the ground was "nowhere near realistic". Ground-level shots immediately showed a pale uniform sheet with sparse spiky grass, plus a colour-space bug (grass colours linearised twice) that the overview hid.

**Suggested improvement:** A graphics art-pass skill should require a fixed set of verification viewpoints from the first iteration: wide establishing, eye-level and ground-level near the main materials, and a close-up. Every visual change is judged on all of them, using a contact sheet.

**Principle:** Visual quality has to be verified where the viewer will actually look; one flattering camera hides the defects that matter most.

### Observation 2: Parallel subagents need checkpoint-safe handoffs
**Status:** OPEN

**Date:** 2026-09-29
**Session context:** Two background subagents (workbook generator, world/AI rescale) hit an API session limit mid-edit.
**Skill:** superpowers:dispatching-parallel-agents (or a new orchestration note)
**Type:** open-source
**Phase/Area:** Delegation prompts

**Issue:** Both agents stopped without a report. The orchestrator had to rediscover what each had changed from file timestamps and by building and testing the tree. Strict file ownership in the prompts prevented conflicts, but there was no progress record.

**Suggested improvement:** Delegation prompts should ask the agent to append a short progress note to a file it owns (e.g. `<scratch>/<agent>-progress.md`: done / in progress / next) after each milestone, so work can be resumed or taken over after a crash or rate limit.

**Principle:** Any long-running delegated work should leave a durable breadcrumb trail; the final report is the least reliable place to record progress.
