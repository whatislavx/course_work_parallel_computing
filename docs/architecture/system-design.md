# System design

> Status: draft. Language and framework are not chosen yet (see `decisions/0001-backend-language.md`).

## Context

A containerized search system. Clients send queries over HTTP and WebSocket. The server expands queries through a local LLM (Ollama), searches an inverted index built in parallel by a custom thread pool, and returns results.

## Containers

```
+-----------+        HTTP / WebSocket        +----------------------+        HTTP        +-----------+
| CLI       | -----------------------------> | search-server        | -----------------> | ollama    |
| Web UI    |                                | (thread pool,        |  /api/generate     | (LLM)     |
+-----------+                                |  inverted index)     |                    +-----------+
                                             +----------------------+
```

- `search-server`: the backend. Exposes the REST API and the WebSocket endpoint.
- `ollama`: local LLM runtime, used for query expansion.
- CLI and Web UI run on the host or as clients of `search-server`.

## Key quality attributes

- Parallel index build with a configurable thread count.
- Concurrent client handling without races.
- Graceful degradation: if Ollama is down, search still works with the original query.

Details: `components.md` (what each part does) and `data-flow.md` (how a request moves through them).
