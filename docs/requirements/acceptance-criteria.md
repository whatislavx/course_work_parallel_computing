> **Замінено.** Цей документ — рання чернетка. Джерело істини: `docs/specification/` (див. `docs/specification/13-change-log.md`).

# Acceptance criteria

Points come from the grading table in the assignment (up to 60 points for automatic checks, 40 for the defense).

| Area | Points | Done when |
|---|---|---|
| Docker Compose | 5 | `docker compose up` starts the system; `GET /api/health` returns 200. |
| Inverted index | 5 | `POST /api/index` builds the index from text files; `GET /api/stats` returns the document count. |
| Keyword search | 5 | `POST /api/search` with `mode=keyword` returns correct results in the specified JSON format. |
| Thread pool | 5 | The `threads` parameter is honored; build time falls as the thread count rises. |
| Parallel data structure | 5 | Custom structure with concurrent access; no race conditions under sanitizer runs. |
| Query expansion (LLM) | 5 | The query is expanded with keywords and synonyms through Ollama `/api/generate`. |
| WebSocket streaming | 5 | `/ws/search` sends `query_expansion`, `keyword_search`, and `done` in order. |
| CLI client | 5 | `./search "query"` prints results. |
| Web UI | 5 | Search form, results, and stage visualization over WebSocket. |
| Instructions and clean code | 5 | README build and run steps work on a clean machine; Conventional Commits; one code style. |
| Scalability study | 10 | Plots of build time vs threads and throughput vs clients, with bottleneck analysis. |
| Defense | 40 | Theory questions (Appendix B), demo, architecture explanation, UML, commit history. Missing defense caps the grade at 60. |

Repository rule: a repository with no history or very few commits is not accepted.
