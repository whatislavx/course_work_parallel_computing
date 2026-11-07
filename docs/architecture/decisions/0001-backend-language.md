# 0001. Backend language

- Status: **proposed** (not decided)
- Date: 2026-11-07

## Context

The assignment allows C++, Java, or C#. The backend needs a custom thread pool, a custom concurrent inverted index, an HTTP server, a WebSocket endpoint, and an HTTP client for Ollama. The concurrency study should measure real parallel speedup.

## Options

- **C++**: direct control over threads and memory; the course text on concurrency is C++-oriented. More build setup work (CMake, dependencies).
- **Java**: mature threading and concurrency primitives; fewer memory issues; standard HTTP and WebSocket libraries.
- **C#**: good threading model and tooling; similar trade-offs to Java.

## Decision

Pending. Choose before writing code in `src/`. Record the choice here with the reason and update `CLAUDE.md`.
