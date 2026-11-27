<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](Doxygen.zh-CN.md)

# Doxygen Comments

This standard governs API documentation comments on project-authored C++ declarations. Apply it alongside [Epic's C++ Coding Standard](https://dev.epicgames.com/documentation/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine) and our [Public API, Errors, and Documentation](API.md) rules. Write all public API comments in **English**, including comments on Blueprint-exposed declarations.

## Purpose and coverage

Good API comments explain *intent* and *observable contract behavior*, rather than repeating the function name in prose or narrating internal implementation steps. Keep symbol names self-explanatory, and update comments whenever a contract changes.

- **Must** document every public type, consumer-facing method in the first `public:` section, enum and enum value, constant, mod extension contract, and Blueprint entry point. Also document public properties and fields (including `UPROPERTY` members), as well as `protected` declarations whose contract subclasses depend on. Methods placed in a second `public:` section for framework wiring or Unreal lifecycle overrides do not need Doxygen comments, and no `protected:` or `private:` access specifier may appear between the two `public:` sections. Doxygen hides undocumented members automatically without per-symbol exclusion lists.
- **Should** document `private` declarations with Doxygen only when a subtle invariant is not obvious from the code itself. Use plain `//` comments for local implementation notes.
- **Must** declare one variable per line so each field can be documented cleanly. Place every Doxygen comment immediately above its declaration (except for short trailing `///<` comments on fields and enum values). Write a declaration's contract once in the header—do not duplicate it above the `.cpp` definition.
- **Must** place a Doxygen `@file` comment right below the copyright header in every public header file so file-scope declarations appear in the generated reference.

## Comment forms and line limits

Pick the comment style based on how much text is needed:

| Form | When and how to use |
| --- | --- |
| `///<` after a field or enum value | Single-line trailing comment with **at most 25 characters** of content. Within an enum, align the first `/` of every `///<` to the same visual column (counting tabs as 4-space stops). If *any* value in an enum needs more than 25 characters, switch *all* values in that enum to leading comments. |
| `///` above a declaration | Single-line leading comment with **at most 75 characters** of content. Do not add `@brief` or other `@` tags. |
| `/** ... */` above a declaration | Multi-line block used whenever the comment exceeds a single 75-character line. Begin every paragraph with an `@` tag—typically `@brief` first, followed by `@details`, `@par`, or parameter/return tags. Never stack multiple untagged `///` lines. |

Character counts include spaces and `@` tags inside the comment body, excluding leading indentation, the comment delimiter (`///`, `///<`, `/**`, `*`), and the single space after it. Every physical line of comment content caps at **75 characters** (`///<` caps at **25**). Separate paragraphs with a blank `*` line; continuation lines within the same paragraph do not repeat the paragraph's tag. Put each `@` tag at the start of its own line and always use `@` rather than `\` (e.g., `@file`, `@param`, `@return`). Comments placed directly above a declaration do not need `@class`, `@fn`, or `@var`. CI enforces these rules automatically (see [Build Scripts, Tooling, and Review](BuildAndReview.md)).

For example, the comment body below is exactly 75 characters and fits on a single `///` line:

```cpp
/// The world keeps the settings chosen by its level until the map is unloaded.
```

By contrast, this 80-character sentence exceeds the single-line limit:

```text
The map retains the world settings chosen by its level until this world unloads.
```

Switch to a tagged `/** ... */` block and wrap cleanly at a word boundary:

```cpp
/** @brief The map retains the world settings chosen by its level
 * until this world unloads.
 */
```

## What to cover

| Declaration | Contract details to include when applicable |
| --- | --- |
| Class or struct | Problem it solves, core responsibility, and typical usage pattern. |
| Method or function | Purpose, allowed calling thread, callback thread, preconditions, observable side effects, and failure behavior. |
| Parameter | Meaning, units, valid bounds, sentinel values, and in/out direction. |
| Return value | Meaning of returned data or status codes. Omit `@return` when `@brief` already makes a simple return value obvious, or when returning `void`. |
| Property, field, or constant | Meaning, units, bounds, special values, ownership, and lifetime. |
| Enum and enumerators | Overall state/outcome domain plus what each individual value means. |
| Asynchronous operation | Cancellation handle, terminal callback guarantees, timeout/error rules, and behavior if cancelled after completion. |

## Common Doxygen tags

Use tags only when they add genuine contract clarity—never treat this table as boilerplate to paste onto every function:

| Tag | Purpose |
| --- | --- |
| `@file FileName.h` | Registers a public header so file-level declarations appear in Doxygen output. |
| `@brief Description` | Opens a multi-line comment with a concise summary. |
| `@details Description` | Starts an extended explanation paragraph after `@brief`. |
| `@par [Title]` | Starts an additional prose paragraph (with an optional heading), placing body text on the next line. |
| `@param Name Description` | Documents an input parameter (`Name` must match the C++ signature). |
| `@param[out] Name Description` | Documents an output parameter. |
| `@param[in,out] Name Description` | Documents a parameter both read and mutated by the call. |
| `@tparam Name Description` | Explains a non-obvious template parameter or constraint. |
| `@return Description` | Explains a return value not already covered by `@brief` (omit for `void`). |
| `@pre Description` / `@post Description` | States a caller precondition or an observable postcondition. |
| `@note Description` | Highlights a helpful usage detail outside the main flow. |
| `@warning Description` | Warns about misuse that could cause bugs, crashes, or data loss. |
| `@see Reference` | Cross-references a closely related type or method. |
| `@deprecated Description` | Marks a symbol deprecated and points callers to its replacement. |

Start each tag on a new line, never leave a tag empty, and never restate a parameter's identifier as its description. Wrap long tag descriptions onto continuation lines to stay within the 75-character content limit:

```cpp
/** @brief Returns whether an activity may start with this player count.
 *
 * @details Call on the game thread. A count below the minimum fails
 * the readiness check; a minimum of zero disables that check.
 *
 * @param PlayerCount Number of admitted players; must be non-negative.
 */
bool CanStartActivity(int32 PlayerCount) const;

/// Maximum admitted players; zero leaves the maximum unrestricted.
int32 MaxPlayers = 0;

enum class ETaskResult
{
    Success,          ///< Task completed.
    RetryableFailure, ///< Retry later.
};
```

If even one enumerator needs more than 25 characters, convert the entire enum to leading comments (and use `/** ... */` for any value exceeding 75 characters):

```cpp
enum class ETaskResult
{
    /// Task completed.
    Success,

    /// Task failed but can be retried after a short delay.
    RetryableFailure,
};
```

## Review

CI checks `@file` headers, first-section method coverage, zero-warning Doxygen generation, and mechanical formatting. Reviewers verify that the English prose accurately captures the C++ contract, ownership model, and runtime behavior. Run the local documentation checks in [Build Scripts, Tooling, and Review](BuildAndReview.md) before opening a pull request.
