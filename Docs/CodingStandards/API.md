<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](API.zh-CN.md)

# Public API, Errors, and Documentation

Design UE-facing C++ contracts first, then expose selected operations to Blueprint when host-game developers or mod authors need them. The independent native mod boundary is a versioned C ABI with fixed-width data, explicit ownership, and capability negotiation. There is no stable 1.0 API guarantee yet; explain contract changes in the pull request and update protocol versions whenever compatibility changes.

## Contracts and ownership

- Use UE types in UE-facing stable interfaces. Keep the native C ABI free of UE object layouts and C++ standard-library types. Provide opaque handles and explicit host services to mod authors where needed.
- State who creates, owns, and destroys each service, handle, and buffer. Modules or Subsystems manage service lifetime. Use `TUniquePtr` or `TSharedPtr` for plain C++ ownership where appropriate; a raw pointer is non-owning. Follow UObject ownership and garbage-collection rules for reflected objects.
- Public methods must document their allowed calling thread and callback thread. Asynchronous public callbacks run on the Game Thread by default.
- Do not capture an unprotected UObject raw pointer in work that may outlive the object. Use a weak reference or another lifetime-safe mechanism.

## Asynchronous operations and errors

- An asynchronous public operation returns a cancellation handle and may accept separate success, cancellation, timeout, and failure callbacks. Callers do not need to supply every callback. Exactly one matching terminal callback runs; an omitted callback is not invoked.
- Propagate cancellation to downstream work where supported. Document what cancellation means when the work has already finished or cannot be stopped.
- Use a unified result model with a domain-specific error enum. Add a Gameplay Tag only for complex classifications, and text detail only when needed. Do not replace basic error codes with arbitrary strings.
- Reserve `check` and `ensure` for programmer errors or internal invariants. Treat invalid mod manifests, incompatible versions, failed mounts, and external I/O failures as ordinary result errors.

## Required API documentation

For a class exposing consumer operations, place consumer-facing methods in its first `public:` section, immediately after the class opening and Unreal reflection macro (when present). Place framework integration and Unreal lifecycle methods in a second `public:` section, with no intervening access specifier. The second section is a C++ access boundary rather than consumer API and does not require Doxygen comments. Keep implementation-only methods `private` or `protected`.

Document every public type, first-section consumer method, enum and enum value, constant, extension contract, and Blueprint entry point in English Doxygen comments. Explain the purpose, parameters and return values, ownership and lifetime, thread expectations, and failure, timeout, and cancellation semantics where relevant. Every public header requires an `@file` comment. Document native ABI layouts, version negotiation, buffer ownership, and callback lifetimes in the protocol specification before exposing them.

Follow the [Doxygen comment standard](Doxygen.md). Pull request checks verify the mechanical comment rules and build the Doxygen HTML; reviewers verify that the text accurately describes the actual contract.
