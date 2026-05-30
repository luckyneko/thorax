# Global rules

This repository is **Thorax**, a C++17 cross-platform plugin framework. The rules below are general working discipline that applies to every change here. Project-specific architecture, the plugin ABI and DSO-lifetime contracts, build & test commands, and naming/style conventions live in **[CLAUDE.md](CLAUDE.md)** — that file is authoritative; read it first and defer to it wherever it is more specific than this one.

## Top 10 non-negotiables

1. **Ask before planning when uncertainty matters.** If an answer could change architecture, ownership, production call path, performance, or test strategy, ask first. Do not guess through decision-changing ambiguity.
2. **Do not create duplicate or parallel implementations.** No `Foo2`, no `FooNew`, no alternative helpers, no benchmark/test/prototype copy of production logic.
3. **New code must be used by the real production path.** Trace the call path from production entry point to changed implementation before calling the work done.
4. **Tests and benchmarks must exercise production code.** Do not validate copied logic, test-only implementations, or isolated scaffolding the real system does not use.
5. **Read before you write.** Read the whole function, owner, nearby code, and callers before editing. No line patches without context.
6. **Hunt down local idioms before editing.** Inspect nearby files, call sites, naming, allocation/error-handling patterns, test style, ownership boundaries, and formatting. Match them closely enough that the change leaves no stylistic evidence of a different author or tool.
7. **Put behavior where it belongs.** Prefer modifying the right class over wrappers, adapters, sibling classes, free functions, helper files, or caller-side workarounds.
8. **Never weaken tests to get green.** Diagnose whether the code is wrong or the test was wrong from the start. Justify any test expectation change before making it.
9. **Verify through the real caller before claiming completion.** Run the relevant test or command, grep/read the symbols involved, and prove production reaches the new behavior.
10. **Keep the diff coherent.** No drive-by reformatting, unrelated edits, dangling stubs, dead code, or half-done branches. Delete what you replace.

## Clarifying questions

- Ask before planning when the answer could materially change architecture, ownership, production call path, performance, or test strategy. Stop once the remaining uncertainty is not decision-changing — state a reasonable assumption and proceed.
- Before implementing, check the plan against the user's answers. If their answers invalidate it, revise before editing.

## Architecture and reuse

- Before writing a new class, search for an existing owner and extend or generalize it. Duplicate or "alternative" implementations are unacceptable — including in benchmarks, tests, prototypes, examples, and temporary scaffolding.
- **Separation of concerns.** Code lives where it logically belongs. If outside code must reach into a class's internals, the method probably belongs on the class.
- When adding a feature, refactor the nearby code it needs as part of the same change. If the refactor is large, surface the trade-off before proceeding.

## Before editing

Identify:

- the class/function that should own the change;
- the production call path that will reach it;
- overlapping tests, benchmarks, demos, or prototypes;
- local idioms, naming, style, ownership, and formatting patterns.

If overlapping code exists, modify, move, or delete it. Do not create another path.

## Testing and integration

- Test behavior real callers depend on, not scaffolding or implementation details. Exercise the production implementation through the closest practical real caller, and add or update tests for every change.
- Do not edit a failing test just to pass. If the test is genuinely wrong, say why before changing it.
- Do not leave features, prototypes, or test-only implementations disconnected from the system they were built for. If a benchmark, demo, or prototype helped develop the code, integrate it into the production path or delete it so it cannot be mistaken for the real thing.
- Before claiming completion, state which production file/function owns the behavior and which test or command proves that path is used.

## Build & verify

- **Build out-of-source only.** Never configure or build in the source tree; use a separate `build/` dir. (Exact commands and CMake options live in CLAUDE.md.)
- **Warnings are errors** on every supported compiler — a change that introduces a warning is not done.
- **Keep the docs honest.** When you change a documented contract (the plugin ABI exports, registry ownership/lock policy, DSO-lifetime rules, logging surface, export macros, or layout/install structure), update CLAUDE.md in the same change.
- **Verify, don't assume.** "Should work" doesn't count. Run the test, read the file, grep the symbol, trace the production path.

## Craft and discipline

- **Names carry the architecture.** If a name is awkward, the abstraction is wrong. Fix the shape, not the name length or comment.
- **Match surrounding style.** New code should look native. Follow the nearest `.clang-format` from the file's directory up to the repo root.
- **Finish before you start.** If the task is bigger than expected, surface it. Do not leave dangling stubs or silent partial work.
- **Stop after 2–3 failed attempts.** Repeated failure means the premise may be wrong. Re-read, ask, or change approach.
- **Surface real trade-offs.** When facing perf vs. clarity, generality vs. YAGNI, or refactor now vs. later, name the decision briefly before committing.

## Plans

- **Be concise.** Every line must earn its place. Revise before presenting: cut filler, merge overlap, remove the obvious, and sharpen vague phrasing.
- **State intent and decisions, not narration.** Use concrete steps, named files/functions where they matter, and real choices.
- Reflect the user's answers and constraints; remove any step that contradicts them. State assumptions, and if one could materially change the implementation, ask before proceeding.

## Session

- **Warn at 100K tokens of context.** Offer to compact before continuing so quality does not degrade or auto-compact mid-task.
