<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](Cpp.zh-CN.md)

# C++ Style and Headers

Follow both the [Epic C++ standard](https://dev.epicgames.com/documentation/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine) and the project rules below. Report any conflict in an issue instead of choosing one rule over the other. The project uses clang-format for layout; run the pinned version before submitting a PR.

Use tabs for block indentation in C++ and UE C# build scripts, with a tab width of four characters. Spaces may be used to align text after non-tab characters. This follows Epic's indentation rule and is enforced by the formatter configuration.

## Names and types

- Follow all applicable Unreal type prefixes and naming rules, including `A`, `U`, `F`, `I`, and `E` where required. These prefixes are independent of the optional project prefix.
- Use `Mod::` as the single top-level namespace for project-owned non-reflected C++ types and APIs, adding a nested domain namespace where helpful—for example, `Mod::FModId` and `Mod::IModService`. Unreal Header Tool does not support namespaces for reflected types, so declare reflected types at global scope.
- Types inside `Mod::` and its nested namespaces must not carry the `Nelaric` project prefix; use names such as `Mod::FModId`, not `Mod::FNelaricModId`, while keeping required Unreal type prefixes. The `Nelaric` project prefix is optional for modules, files, globally scoped types, and log categories. Use descriptive names and keep related declarations, files, and references consistent.
- Names must not collide with existing engine, third-party, or project names. A project prefix may be used to distinguish colliding names; for example, `UNelaricModContext` and `UNelaricModComponent` distinguish framework types from Unreal's `UObject` and `UActorComponent`. Check type names, module names, and header filenames before introducing a new name.
- Name modules and log categories consistently after their owning domain. Prefer names that describe mechanism or responsibility rather than a particular game's rules.
- Follow Epic's standard-library guidance. Prefer UE containers and strings, and avoid C++ standard-library containers and strings except in interoperability code. Other standard-library facilities may be used where Epic permits them and they produce better results. UE-facing cross-module APIs use UE types; the external native mod C ABI uses fixed-width C-compatible types. Do not mix UE and standard-library conventions within the same API.
- Prefer typed constants and `constexpr` over new macros. Use required UE macros normally. Any new project macro requires a documented justification and must follow Epic's uppercase `UE_` naming rule; cross-module feature switches must be defined centrally.

## Headers and dependencies

- Public headers must be self-contained: a consumer should be able to include any public header on its own without relying on incidental include order. Include what the declaration needs and forward-declare where sufficient.
- Place implementation-only includes and declarations in `Private`. A Runtime public header must not include a concrete optional integration or vendor SDK header.
- Keep header dependencies minimal. Do not pull in a broad include solely to obtain a forward-declarable type.
- Follow Unreal Header Tool requirements for reflected declarations, including the placement of generated headers. These requirements take precedence over mechanical include sorting.

## UCLASS exports

- Every project-authored `UCLASS` must explicitly include `MinimalAPI` in its `UCLASS(...)` declaration. This applies across all modules and to classes in both `Public` and `Private`, including abstract, internal, editor, and test classes.
- Do not place the owning module's `*_API` macro on a `UCLASS` class declaration. Whole-class export is forbidden; retain `MinimalAPI` even when adding cross-module functionality.
- Export only individual non-inline methods that require cross-module C++ linkage by placing the owning module's `*_API` macro on those method declarations. Include constructors and other methods required for supported cross-module derivation where necessary. Do not export implementation-only methods merely because they are public or reflected.
- `MinimalAPI` controls native symbol exports; it does not replace `UFUNCTION` or Blueprint exposure specifiers. Type visibility alone does not export non-inline method implementations. See Epic's [class specifiers](https://dev.epicgames.com/documentation/unreal-engine/class-specifiers) for the engine semantics.

## Source text

Code and human-written documentation use UTF-8 with BOM and CRLF. Tool configuration and executable scripts may use UTF-8 without BOM and LF where required for interoperability; see [Build scripts and tooling](BuildAndReview.md) for the list of exceptions. Do not mix newline styles within a file.
