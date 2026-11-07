<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](API.zh-CN.md)

# Public API, Errors, and Documentation

Design UE-facing C++ contracts first, then expose selected operations to Blueprint when host-game developers or mod authors need them. The independent native mod boundary is a versioned C ABI with fixed-width data, explicit ownership, and capability negotiation. No stable 1.0 API promise exists yet; explain contract changes in the pull request and update protocol versions when compatibility changes.

## Contracts and ownership

- Use UE types in UE-facing stable interfaces. Keep the native C ABI free of UE object layouts and C++ standard-library types. Give mod authors opaque handles and explicit host services where needed.
- State who creates, owns, and destroys each service, handle, and buffer. Modules or Subsystems manage service lifetime. Use `TUniquePtr` or `TSharedPtr` for plain C++ ownership where appropriate; a raw pointer is non-owning. Follow UObject ownership and garbage-collection rules for reflected objects.
- Public methods must state their allowed calling thread and callback thread. The default for asynchronous public callbacks is the Game Thread.
- Do not capture an unprotected UObject raw pointer in work that may outlive it. Use a weak reference or another lifetime-safe mechanism.

## Asynchronous operations and errors

- An asynchronous public operation returns a cancellation handle and may accept separate success, cancellation, timeout, and failure callbacks. Callers need not supply every callback. Exactly one matching terminal callback runs; an omitted callback produces no call.
- Propagate cancellation to downstream work where supported. Document what cancellation means if work already finished or cannot be stopped.
- Use a unified result model with a domain-specific error enum. Add a Gameplay Tag for complex classifications and text detail only when needed. Do not replace basic error codes with arbitrary strings.
- Use `check` and `ensure` for programmer mistakes or internal invariants. Invalid mod manifests, incompatible versions, failed mounts, and external I/O failures are ordinary result errors.

## Required API documentation

For a class exposing consumer operations, put consumer-facing methods in its first `public:` section, directly after the class opening and Unreal reflection macro when present. Put framework integration and Unreal lifecycle methods in a second `public:` section, with no intervening access specifier. The second section is a C++ access boundary, not consumer API, and does not require Doxygen comments. Keep implementation-only methods private or protected.

Document every public type, first-section consumer method, enum and enum value, constant, extension contract, and Blueprint entry point in English Doxygen. Explain purpose, parameters and returns, ownership and lifetime, thread expectations, and failure, timeout, and cancellation semantics when relevant. Each public header needs an `@file` comment. Document native ABI layouts, version negotiation, buffer ownership, and callback lifetime in the protocol specification before exposing them.

Follow the [Doxygen comment standard](Doxygen.md). Pull request checks verify the mechanical comment rules and build Doxygen HTML; reviewers verify that the text describes the actual contract.
