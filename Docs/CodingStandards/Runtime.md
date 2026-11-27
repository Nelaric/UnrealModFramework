<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](Runtime.zh-CN.md)

# Runtime, Performance, and Security

## Concurrency and reliability

- **Clean shutdown boundaries:** Every runtime service must document its lifetime and teardown order. Never fire callbacks into a module, `GameInstance`, or `UWorld` after it has started shutting down.
- **Game Thread by default:** Public asynchronous callbacks run on the Game Thread unless explicitly documented otherwise. Background tasks must marshal results back to the Game Thread before reading or mutating game-thread-only Unreal objects.
- **Bounded external work:** Any external or cross-process request must enforce an explicit timeout, bounded retry count, and bounded queue capacity. Retry only idempotent or safely repeatable operations, forward cancellation downstream where supported, and never spawn unbounded background tasks or indefinite waits.
- **Treat external inputs as untrusted:** `.mod` headers, manifests, archive indexes, cooked metadata, remote preview payloads, and user configuration files are all untrusted inputs. Enforce strict byte and element limits, verify normalized paths and SHA-256 digests before mounting, and report malformed inputs as ordinary result errors rather than fatal assertions. Keep in mind that native mod DLLs execute with the full privileges of the host game process—cryptographic signatures verify provenance, not sandboxing.

## Hot paths

- **Zero blocking I/O:** `Tick`, network callbacks, Hook dispatch paths, and other per-frame or high-frequency code must never perform blocking disk, database, or network I/O.
- **Predictable per-frame cost:** Avoid unbounded heap allocations, unbounded loops, and per-frame verbose logging on hot paths. Push heavy preparation or I/O onto asynchronous setup stages while keeping Unreal's native replication pipeline intact.
- **Documented exceptions:** If a hot path genuinely requires an exception to the rules above, document its worst-case cost and upper bounds in the pull request.

## Console commands

- Prefix all project-defined Unreal console commands and CVars with lowercase `ng.` (for example, `ng.perf.stat`).

## Logging, configuration, and secrets

- **Keep sensitive data out of logs:** Never log access tokens, server credentials, cryptographic keys, or player personal data. Emit concise, structured diagnostic fields focused on what is needed to debug the issue.
- **Centralize secret handling:** Read configuration and credentials through a dedicated service, pass optional integrations only the specific values they need, and never copy secrets into general runtime metadata or user-facing error strings.
- **Audit privileged actions:** Security-sensitive and administrative operations must verify authorization up front and record a clear audit trail without leaking secret values.
