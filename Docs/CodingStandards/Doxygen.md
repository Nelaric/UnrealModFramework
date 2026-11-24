<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](Doxygen.zh-CN.md)

# Doxygen Comments

This standard applies to project-authored C++ declarations and their API documentation. Follow it alongside [Epic's C++ coding standard](https://dev.epicgames.com/documentation/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine) and the [public API contract rules](API.md). Write public API comments in English, including comments on Blueprint-exposed declarations.

## Purpose and coverage

Comments explain intent and observable behavior rather than restating a declaration's spelling or implementation. Keep names descriptive and update comments whenever a contract changes.

- **Must** document every public type, consumer-facing method in the first `public:` section, enum and enum value, constant, mod extension contract, and Blueprint-exposed entry point. Document public properties and fields, including reflected properties, and document protected declarations when derived types rely on their contract. Methods in a second `public:` section reserved for framework integration or Unreal lifecycle calls do not require Doxygen comments, and no other access specifier may appear between the two `public:` sections. Doxygen automatically hides undocumented members without requiring a per-symbol exclusion list.
- **Should** document private declarations only when their intent or invariant is not clear from the code. Use ordinary `//` comments for local implementation details when needed; they are not API documentation.
- **Must** place each variable declaration on its own line so its meaning can be documented independently. Place each comment immediately before its declaration, except for the short trailing variable and enum comments described below. Document a public declaration once; do not repeat the same contract at its definition.
- **Must** start each public header with a Doxygen `@file` comment immediately after the copyright notice so file-level declarations have a documentation context.

## Form and line length

Choose the form by the amount of text needed:

| Form | Rule |
| --- | --- |
| `///<` after a variable or enum value | At most 25 characters of comment content on one line. Align the first `/` of every `///<` comment in the same enum to the same visual column. If any enum value needs more than 25 characters, switch every value in that enum to a leading comment. |
| `///` on the line before a declaration | At most 75 characters of comment content on one line. No `@` tag is required. |
| `/** ... */` before a declaration | Use when the description does not fit in a single 75-character line. Start every paragraph with a Doxygen `@` tag: typically `@brief` first, followed by `@details`, `@par`, or a specific tag. Do not substitute multiple untagged `///` lines. |

For every form, count spaces and Doxygen tags, but exclude indentation, the comment marker, and its separating space. Each physical comment-content line has a 75-character maximum, while `///<` has the stricter 25-character maximum. Count tabs as four-column stops when aligning enum comments. A blank comment line separates paragraphs; wrapped lines within the same paragraph do not repeat its tag. Place each tag on its own line and use `@` consistently for commands such as `@file`, `@param`, and `@return`. A comment directly preceding its declaration does not need `@class`, `@fn`, or `@var`. The [CI style check](BuildAndReview.md#local-checks) enforces these mechanical rules.

The comment text in this example is exactly 75 characters:

```cpp
/// The world keeps the settings chosen by its level until the map is unloaded.
```

This sentence has exactly 80 characters and exceeds the limit on one line:

```text
The map retains the world settings chosen by its level until this world unloads.
```

Wrap the same sentence at a word boundary without changing its meaning:

```cpp
/** @brief The map retains the world settings chosen by its level
 * until this world unloads.
 */
```

## What to explain

| Declaration | Required content when relevant |
| --- | --- |
| Class or struct | The problem it solves, its responsibility, and how callers use it. |
| Method or function | Purpose, valid calling thread, callback thread, preconditions, observable side effects, and failure behavior. |
| Parameter | Meaning, unit, valid range, special values, and input or output role. |
| Return value | Meaning of results and status values. Omit `@return` when the purpose statement already explains a simple result. |
| Property, field, or constant | Meaning, unit, range, special values, ownership, or lifetime. |
| Enum and value | The states or outcomes represented and the meaning of each value. |
| Asynchronous operation | Cancellation handle, terminal callbacks, timeout and failure semantics, and behavior when cancellation comes too late. |

## Common Doxygen tags

Use these tags when they add useful contract information; they are not a checklist to include in every comment.

| Tag | Use |
| --- | --- |
| `@file FileName.h` | Identify a public header so file-level declarations appear in the API documentation. |
| `@brief Description` | Give the short summary of a multi-line comment. |
| `@details Description` | Start a detailed prose paragraph after the summary. |
| `@par [Title]` | Start another prose paragraph, optionally with a heading. Put its text on the following line. |
| `@param Name Description` | Describe an input parameter; the name must match the declaration. |
| `@param[out] Name Description` | Describe an output parameter. |
| `@param[in,out] Name Description` | Describe a parameter read and modified by the function. |
| `@tparam Name Description` | Explain a template parameter whose role or constraints are not evident. |
| `@return Description` | Explain a return value when the purpose statement does not already make its meaning clear. Omit for `void`. |
| `@pre Description` / `@post Description` | State a caller precondition or an observable guarantee after the call. |
| `@note Description` | Add a useful usage detail that is not part of the main description. |
| `@warning Description` | Call out a condition that may lead to incorrect use or data loss. |
| `@see Reference` | Link a directly related type or operation. |
| `@deprecated Description` | State that an API is deprecated and name its replacement or migration path. |

Place each tag on its own comment line. Do not add empty tags or repeat a parameter or symbol name as its description. Keep tag text within the 75-character comment-content limit by wrapping longer explanations onto continuation lines.

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

Whenever any enum value needs more than 25 characters, use leading comments for every value in the enum. Any value needing more than 75 characters uses a tagged `/** ... */` block under the general rule.

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

CI checks public-header `@file` comments, the presence of first-section method comments, Doxygen documentation errors, and mechanical comment style. Reviewers judge whether the English comments accurately describe the code contract, including relevant ownership and runtime behavior. Run the [documentation and text checks](BuildAndReview.md#local-checks) relevant to the change. A warning-free Doxygen build does not replace content review.
