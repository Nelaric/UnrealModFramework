<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](Cpp.zh-CN.md)

# C++ Style and Headers

Follow both [Epic's C++ Coding Standard](https://dev.epicgames.com/documentation/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine) and the project rules below. If the two ever disagree, report the conflict in an issue rather than picking one side ad hoc. Run the repository's pinned `clang-format` before opening a pull request.

Indent code blocks in C++ and Unreal C# build scripts with tabs set to a 4-character stop; spaces are permitted after non-tab characters for alignment. Both match Epic's indentation rule and are enforced by our formatter config.

## Naming and types

- **Keep Unreal type prefixes:** Apply standard Unreal prefixes (`A`, `U`, `F`, `I`, `E`, etc.) wherever Epic's rules call for them, regardless of whether a name also uses a project prefix.
- **Namespace non-reflected C++ APIs under `Mod::`:** Put project-owned non-reflected types and functions inside the top-level `Mod::` namespace, adding nested domain namespaces where helpful (for example, `Mod::FModId` or `Mod::Loader::IModLoaderModule`). Because Unreal Header Tool does not support namespaces on reflected types, keep `UCLASS`, `USTRUCT`, `UENUM`, and `UINTERFACE` declarations at global scope.
- **Avoid redundant prefixes inside `Mod::`:** Never add `Nelaric` to names already inside `Mod::`—write `Mod::FModId`, not `Mod::FNelaricModId`, while keeping the `F`/`I`/`E` prefix. Outside `Mod::` (modules, source files, global reflected types, and log categories), the `Nelaric` prefix is optional; use it when needed to prevent collisions with Unreal or third-party symbols (such as `UNelaricModContext` or `UNelaricModComponent`). Always check existing engine, plugin, and project names before introducing a new identifier.
- **Name by mechanism, not by game:** Name modules and log categories consistently after the domain responsibility they own rather than a specific game's mechanics.
- **Prefer Unreal containers and strings:** Follow Epic's standard-library guidance—use UE containers and string types in UE modules, reserving `std::` containers and strings for external interoperability layers and standalone tools. Other C++ standard-library utilities are welcome where Epic allows them and they make the code clearer. Keep each boundary consistent: UE-facing cross-module APIs use UE types, while the external C ABI uses fixed-width C types.
- **Prefer `constexpr` and typed constants over macros:** Use standard Unreal reflection and module macros normally, but avoid inventing new preprocessor macros when `constexpr` values, inline functions, or templates work. Any new project macro must be justified in review, follow Epic's uppercase `UE_` naming convention, and live in a central header if shared across modules.

## Header hygiene and dependencies

- **Self-contained public headers:** Every header under `Public/` must compile cleanly when included first in an otherwise empty translation unit. Include everything the header's own declarations directly need, and forward-declare wherever a pointer or reference suffices.
- **Keep implementation details private:** Put internal headers and heavy includes under `Private/`. Public Runtime headers must never pull in optional integration headers or third-party vendor SDK headers.
- **Respect UHT include order:** Follow Unreal Header Tool rules for reflected headers—especially keeping `*.generated.h` as the last `#include` in the header—even when mechanical include sorting would place it elsewhere.

## `UCLASS` symbol exports

- **Always specify `MinimalAPI`:** Every project-authored `UCLASS`—whether in `Public` or `Private`, and including abstract, editor, internal, and test classes—must include `MinimalAPI` in its `UCLASS(...)` specifier list.
- **Never export an entire `UCLASS`:** Do not place a module `*_API` macro on the `class` keyword of a `UCLASS`. Keep `MinimalAPI` even when methods on the class are called across modules.
- **Export individual methods selectively:** Add the module's `*_API` macro only to the specific non-inline methods (and constructors or virtual overrides needed for supported cross-module subclassing) that require cross-module C++ linkage. Do not export internal helper methods just because they happen to be `public` or `UFUNCTION`.
- **Understand what `MinimalAPI` controls:** `MinimalAPI` governs native C++ symbol exports only—it does not replace `UFUNCTION` or Blueprint visibility specifiers, and type metadata visibility alone does not export non-inline method bodies (see Epic's [Class Specifiers](https://dev.epicgames.com/documentation/unreal-engine/class-specifiers) documentation).

## File encoding and line endings

C++ source/headers and human-written Markdown files use **UTF-8 with BOM** and **CRLF** line endings. Tooling configs, JSON descriptors, and scripts use **UTF-8 without BOM** and **LF** where required for cross-platform tool compatibility; see [Build Scripts, Tooling, and Review](BuildAndReview.md) for the exact breakdown. Never mix CRLF and LF in the same file.
