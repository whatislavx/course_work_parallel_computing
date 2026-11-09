> **Замінено.** Цей документ — рання чернетка. Джерело істини: `docs/specification/` (див. `docs/specification/13-change-log.md`).

# Functional requirements

Source: Appendix A of `Курсова_ПО_2026-2027.pdf`.

| ID | Requirement | Source in assignment |
|---|---|---|
| FR-1 | The backend is written in C++, Java, or C#. | Основні вимоги, п. 1 |
| FR-2 | `docker compose up` starts the search server and an Ollama service. | п. 2 |
| FR-3 | `GET /api/health` returns `{ "status": "ok", "ollama": true\|false }`. | п. 2, 4 |
| FR-4 | `POST /api/index` builds the inverted index in parallel with a custom thread pool. The `threads` parameter sets the pool size. | п. 3, 4 |
| FR-5 | The inverted index is our own data structure and supports concurrent access from multiple threads. | п. 3 |
| FR-6 | `POST /api/search` with `mode: "keyword"` returns ranked results with `doc_id`, `score`, `snippet`, and `time_ms`. | п. 4 |
| FR-7 | `GET /api/stats` returns the number of indexed documents, index build time, and threads used. | п. 4 |
| FR-8 | Query expansion calls Ollama `/api/generate` to add keywords and synonyms to the query. | п. 5 |
| FR-9 | `WebSocket /ws/search` streams each pipeline stage as a separate message with `stage` and `data`: `query_expansion`, `keyword_search`, `done`. | п. 6 |
| FR-10 | The server handles requests from several clients at the same time. | Завдання, мета |
| FR-11 | CLI client: `./search "query"` prints the results from the command line. | п. 7 |
| FR-12 | Web UI: a search form, a results view, and a visualization of pipeline stages received over WebSocket. | п. 7 |
