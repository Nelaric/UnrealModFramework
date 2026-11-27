<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](API.zh-CN.md)

# Public API, Errors, and Documentation

Design clean, idiomatic Unreal C++ contracts first, then expose selected operations to Blueprints where host-game developers or mod authors genuinely need them. Across the standalone native mod boundary, use a versioned C ABI built on fixed-width types, explicit buffer ownership, and capability negotiation. Because the project has not yet frozen a 1.0 API, describe every contract change in your pull request and bump protocol versions whenever wire or ABI compatibility changes.

## Contracts and ownership

- **Keep UE types inside UE boundaries:** Use standard Unreal types in UE-facing C++ interfaces, and keep UE object layouts and C++ standard-library types completely out of the native C ABI. Expose opaque handles and explicit host callback tables to native mods instead.
- **Make ownership explicit:** Document who creates, owns, and destroys every service, handle, and buffer. Modules or UE Subsystems own service lifetimes. For plain C++ objects, express ownership with `TUniquePtr` or `TSharedPtr`; treat raw pointers as non-owning borrows. Reflected objects follow standard `UObject` ownership and garbage-collection rules.
- **Specify threading:** Every public method must document which thread(s) may call it and which thread delivers its callbacks. Unless documented otherwise, public asynchronous callbacks run on the Game Thread.
- **Guard asynchronous UObject lifetimes:** Never capture a bare `UObject*` in work that might outlive the object; use `TWeakObjectPtr` or another lifetime-safe handle.

## Asynchronous operations and error handling

- **Cancellation and terminal callbacks:** Asynchronous public operations return a cancellation handle and may accept separate callbacks for success, cancellation, timeout, and failure. Callers may omit any callback they do not need; exactly one terminal callback fires per operation, and omitted callbacks are silently skipped.
- **Propagate cancellation:** Forward cancellation requests to downstream work whenever supported, and document what happens if cancellation arrives after work has completed or passed a point of no return.
- **Structured errors over strings:** Return a unified result type backed by a domain-specific error enum. Attach a Gameplay Tag only when hierarchical classification is needed, and include human-readable detail strings only as diagnostic context—never replace structured error codes with free-form text.
- **Assertions versus runtime errors:** Reserve `check` and `ensure` strictly for programmer bugs and broken internal invariants. Malformed `.mod` manifests, version mismatches, failed mounts, and external I/O failures are expected runtime conditions and must be reported through normal error results.

## Required API documentation

When a class exposes operations to external consumers, place all consumer-facing methods in its **first `public:` section**, right after the opening brace and `GENERATED_BODY()` macro (if any). Place framework-internal wiring and Unreal Engine overrides in a **second `public:` section** immediately afterward, with no `protected:` or `private:` section in between. That second `public:` block serves purely as a C++ access mechanism rather than consumer API and does not require Doxygen comments. Keep implementation helpers `protected` or `private`.

Write English Doxygen comments for every public type, first-section consumer method, enum and enum enumerator, constant, mod extension contract, and Blueprint entry point. Cover purpose, parameters, return values, ownership/lifetime, thread constraints, and failure/timeout/cancellation behavior wherever applicable. Start every public header with an `@file` comment, and document native C ABI struct layouts, version negotiation, buffer ownership, and callback lifetimes in the protocol specification before exposing them.

Follow the formatting rules in [Doxygen Comments](Doxygen.md). CI validates mechanical layout and builds the Doxygen HTML without warnings; code reviewers verify that comments match real runtime behavior.
