---
name: cpp-cleanup
description: Clean up and enforce FlowSpace C++ code quality (style, naming, structure, comments, formatting). Use when refactoring C++ code, normalizing style, removing noisy comments, or applying repo quality rules.
---

# C++ Cleanup (FlowSpace)

Apply this skill when cleaning up or reviewing C++ code in this repo. Preserve behavior unless the task explicitly changes it.

## Scope and safety
- Only touch first-party code: `src/`, `tests/`, `scripts/`, and repo docs/config as requested. **Never modify `external/` or generated `build/` output.**
- Read the relevant files and `AGENTS.md` / `docs/engineering/CODE_QUALITY.md` before editing.
- Keep changes minimal and local; do not reformat unrelated code just because it differs.
- Preserve single-threaded design, recovery-journal ordering, atomic config writes, and per-monitor invariants (see `AGENTS.md`).

## Quality rules to enforce
- **Structure:** keep code in the correct layer (`core`, `platform`, `engine`, `ui`, `app`). One clear responsibility per type/file; pair headers/impls as `PascalCase.h/.cpp`.
- **Naming:** namespaces `flowspace::core|platform|engine|ui`; types/enums `PascalCase`; methods/functions `PascalCase`; locals/params `camelCase`; data members `camelCase_`; constants `kPascalCase` (follow local file conventions when established).
- **Comments:** remove redundant or obvious comments. Keep only short, useful comments (1-3 lines) for intent, invariants, or non-obvious Win32 behavior. Prefer clear names over comments.
- **Formatting:** follow `.clang-format` and existing project style (Allman braces, 4-space indent, UTF-8). Keep simple conditions, calls, and initializers on one line when readable; avoid unnecessary line wrapping.
  - Prefer: `if (RegQueryValueExW(key, name.c_str(), nullptr, &type, nullptr, &size) != ERROR_SUCCESS || type != REG_SZ) {`
  - Avoid splitting such conditions across lines without a readability benefit.
- **Code quality:** use RAII for resources/handles, early returns to reduce nesting, `const`/`[[nodiscard]]` where appropriate, no exceptions for control flow, no unused includes/variables/dead code, and small focused helpers instead of duplication.
- **Win32 specifics:** `UNICODE`/wide APIs, validate handles/HRESULT/Win32 results, and log failures with context via spdlog (matching surrounding style).

## Workflow
1. Identify the target files and the specific cleanup goals.
2. Apply rule-based edits (naming, structure, dead code, comment cleanup, formatting fixes).
3. If available, run formatting on touched files only: `clang-format -i <files>` (or `scripts/format.ps1 -Files ...`).
4. Build/test what is relevant:
   - `powershell -ExecutionPolicy Bypass -File scripts\build.ps1`
   - `& build\cmake\flowspace_tests.exe` (or `ctest --test-dir build\cmake`) when core/platform behavior is affected.
5. Summarize changes, note any behavior risks, and report verification results.
