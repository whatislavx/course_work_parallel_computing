# Architecture constraints

- Layers: HTTP/WebSocket transport → search service → index and thread pool → Ollama client. Transport code does not touch index internals directly.
- The inverted index is our own data structure. Concurrent access goes through explicit synchronization (fine-grained locks, sharding, or lock-free where justified). Document the choice in `docs/architecture/decisions/`.
- The thread pool is our own implementation: a fixed set of worker threads, a task queue, and a configurable size (the `threads` parameter of `POST /api/index`).
- Ollama is called over HTTP via `/api/generate`. A query expansion failure must not break keyword search: fall back to the original query and report it in the stage data.
- Each external dependency (Ollama, dataset loader) sits behind an interface so tests can replace it.
