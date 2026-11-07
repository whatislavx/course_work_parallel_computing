# Tasks

Each task has a done criterion. Mark a task done only when its criterion passes. Task IDs go into commit messages.

## Setup

- [x] T-000: Repository structure, `.gitignore`, README stub
- [x] T-001: Project docs: requirements, architecture, API, roadmap
- [ ] T-002: Choose backend language and record it in `docs/architecture/decisions/0001-backend-language.md`
- [ ] T-003: Install Docker and Ollama; pull a small model; confirm `ollama` responds locally
- [ ] T-004: Obtain dataset per course instructions (at least 1 GB, 10 000+ English documents) and store outside git

## Server skeleton

- [ ] T-010: Minimal server with `GET /api/health`
- [ ] T-011: `docker-compose.yml` with server and Ollama services; `docker compose up` works
- [ ] T-012: Health reports `ollama: true|false` based on a real check

## Index

- [ ] T-020: Tokenizer and document reader
- [ ] T-021: Sequential inverted index (single thread) and `POST /api/index` with stats
- [ ] T-022: `GET /api/stats` returns documents, build time, threads used

## Search

- [ ] T-030: Keyword search with ranking and snippets; `POST /api/search`
- [ ] T-031: Error responses for malformed input (after confirming the error format)

## Parallelism

- [ ] T-040: Own thread pool with configurable size
- [ ] T-041: Parallel index build honoring `threads`; equality test against sequential index
- [ ] T-042: Thread-safe inverted index; sanitizer run clean
- [ ] T-043: Concurrent client handling; test with several simultaneous requests

## LLM and streaming

- [ ] T-050: Ollama client for `/api/generate`; query expansion with fallback
- [ ] T-051: WebSocket `/ws/search` with stages `query_expansion`, `keyword_search`, `done`

## Clients

- [ ] T-060: CLI client `./search "query"`
- [ ] T-061: Web UI with search form, results, and stage visualization

## Measurement and documentation

- [ ] T-070: Benchmark script for build time vs threads; plot committed
- [ ] T-071: Benchmark script for throughput vs clients; plot committed
- [ ] T-072: Test table: query, expected result, actual result
- [ ] T-073: UML diagrams: use case, class, sequence per use case, deployment
- [ ] T-074: README build and run instructions verified on a clean checkout
- [ ] T-075: Report PDF with sections 1–5 and link to repository
