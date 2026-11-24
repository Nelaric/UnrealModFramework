<!-- Copyright (c) 2026 Nelaric Contributors -->

English | [简体中文](Runtime.zh-CN.md)

# Runtime, Performance, and Security

## Concurrency and reliability

- A service must document its lifetime and shutdown behavior. Avoid invoking callbacks into a module or world after that owner has begun teardown.
- Public asynchronous callbacks default to Game Thread delivery. Work running on other threads must explicitly marshal results back before touching UE game-thread-only objects.
- Every external request must specify an explicit timeout along with bounded retries and bounded queue growth. Retry only when the operation is safe to repeat, and propagate cancellation to downstream work where supported. Avoid indefinite waits and unbounded background work.
- Treat `.mod` headers, manifests, indexes, cooked metadata, remote preview messages, and configuration as untrusted input. Enforce size and count bounds, validate paths and hashes before mounting, and return ordinary errors for invalid data. Native DLLs execute with the host process's privileges; signatures do not sandbox them.

## Hot paths

- Tick, network callbacks, and other high-frequency paths must not perform blocking file, database, or network I/O.
- Avoid unbounded allocations, unbounded loops, and per-frame verbose logging on those paths. Offload blocking or lower-frequency work to an appropriate asynchronous path and preserve the UE replication path.
- Any justified hot-path exception must explain its cost and bounds in the PR.

## Console commands

- Project-defined console command names must begin with the lowercase prefix `ng.`; for example, a performance statistics command may be named `ng.perf.stat`.

## Logs, configuration, and secrets

- Never log access tokens, server credentials, secrets, or players' personal information. Log only the structured diagnostic fields needed to investigate an issue.
- Read and manage configuration and secrets through a central service. Provide each optional integration only the values it requires, and avoid copying secrets into general runtime metadata or error messages.
- Administrative and security-sensitive operations must verify authorization and leave an appropriate audit trail without recording secret material.
