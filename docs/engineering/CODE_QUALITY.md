# FlowSpace — C++ Code Quality Rules

Practical quality rules for first-party C++ code in this repo (`src/`, `tests/`, `scripts/`). These complement [AGENTS.md](../../AGENTS.md) and [ARCHITECTURE.md](ARCHITECTURE.md); inspect actual code before changing it.

## 1. Folder and file structure
- **Layers:** `src/core` (domain/persistence, no Win32 UI), `src/platform` (Win32/COM), `src/engine` (orchestration), `src/ui` (Win32/GDI UI), `src/app` (entry/wiring). Keep dependencies flowing downward as documented.
- **Files:** one primary type/responsibility per header+source pair. Use `PascalCase` file names matching the main type/module (e.g. `WorkspaceManager.h/.cpp`, `StringUtils.h/.cpp`).
- **Tests/scripts:** tests live in `tests/` (`test_*.cpp`), automation in `scripts/` (`kebab-case.ps1`).
- **Vendored code:** `external/` is third-party and read-only for app cleanup tasks; do not reformat or refactor it.

## 2. Naming conventions
- **Namespaces:** `flowspace::core`, `flowspace::platform`, `flowspace::engine`, `flowspace::ui` (plus anonymous namespaces for file-local helpers).
- **Types:** `PascalCase` for classes, structs, enums, and type aliases (e.g. `WorkspaceEngine`, `WindowInfo`, `HideMode`).
- **Functions/methods:** `PascalCase` (e.g. `EnumerateMonitors`, `IsRegistered`, `GetCommand`).
- **Variables/parameters:** `camelCase` (e.g. `stableId`, `sizeNeeded`).
- **Data members:** `camelCase_` with trailing underscore (e.g. `workspaces_`, `messageWindow_`, `valueName_`).
- **Constants:** prefer `kPascalCase` for local/static constants when adding new ones; follow established nearby names (e.g. `kRunKey`).
- **Helpers:** give helpers specific, verb-based names that describe intent (`QueryTargetMetadata`, `IsEffectivelyVisible`). Place reusable helpers in the appropriate module/util file; avoid duplicating logic across layers.

## 3. Comments and documentation
- **Default:** write self-documenting code; remove comments that restate what the code already says.
- **Keep:** short comments (1-3 lines) only when they explain *why*, document an invariant/precondition, warn about ordering/threading/Win32 quirks, or clarify non-obvious behavior.
- **Avoid:** commented-out code, stale TODOs without context, and long narrative blocks. Public API comments are fine when brief and accurate.

## 4. Formatting and readability
- Follow `.clang-format` (4 spaces, no tabs, Allman braces, ~120 column limit). Run formatting on touched first-party files when `clang-format` is available.
- **Keep it compact:** do not introduce unnecessary line breaks. Simple `if` conditions, function calls, and initializers should stay on one line when readable.
  - Prefer: `if (RegQueryValueExW(key, valueName_.c_str(), nullptr, &type, nullptr, &size) != ERROR_SUCCESS || type != REG_SZ) {`
  - Instead of splitting the condition across lines without need.
- Use early returns/guard clauses to reduce nesting. Keep functions focused; extract helpers when logic becomes hard to follow.
- Prefer consistent include grouping: matching header, project headers, third-party, then Windows/system headers as appropriate for the file (do not churn unrelated includes).

## 5. C++ and Win32 practices
- **Error handling:** no exceptions for control flow. Return `bool`/`std::optional`/`HRESULT`/status values and log actionable context with spdlog.
- **Resource management:** RAII for handles, COM objects, registry keys, DCs/bitmaps, and other OS resources. Do not leak on early returns.
- **Correctness:** mark methods `const`/`override`/`noexcept` and values `[[nodiscard]]` where it matches intent; avoid unused parameters/variables and dead code.
- **Win32/project rules:** preserve `UNICODE`/wide-string APIs, UTF-8 boundary conversions via `core::WideToUtf8`/`Utf8ToWide`, single-threaded message-loop assumptions, and the safety invariants in `AGENTS.md`.

## 6. Verification
- Prefer targeted verification: build (`scripts\build.ps1`) and run `build\cmake\flowspace_tests.exe` when core/platform logic changes.
- For UI/engine integration behavior (hotkeys, hide/show, wallpaper, recovery), follow [TESTING.md](TESTING.md) as applicable.
- Do not claim checks passed unless they were actually run.
