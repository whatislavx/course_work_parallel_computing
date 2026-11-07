# course_work_parallel_computing

Containerized search system with a parallel inverted index, a custom thread pool, a thread-safe data structure, and LLM-based query expansion through Ollama. Coursework for «Паралельні обчислення», КПІ ім. Ігоря Сікорського, ІПСА.

## Features

- Parallel inverted index build with a configurable number of threads
- Concurrent client handling
- Keyword search over REST
- Query expansion through a local Ollama model
- WebSocket streaming of pipeline stages
- CLI client and Web UI

## Quick start

> To be completed when the stack is chosen. Target: `docker compose up`.

## Project layout

```
.claude/rules/   Working rules for development, architecture, code, tests, git
docs/            Requirements, architecture, API contract, development plan
src/             Backend source
tests/           Automated tests
benchmarks/      Scripts that reproduce the scalability study
scripts/         Helper scripts (formatting, dataset preparation)
docker-compose.yml
```

Start with `docs/requirements/` and `docs/development/tasks.md`.
