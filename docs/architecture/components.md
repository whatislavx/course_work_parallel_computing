# Components

| Component | Responsibility | Notes |
|---|---|---|
| HTTP layer | Routes `/api/*`, parses JSON, maps errors to status codes. | Thin; no index logic. |
| WebSocket handler | Serves `/ws/search`, sends one message per pipeline stage. | Uses the same search service as HTTP. |
| Search service | Runs the pipeline: query expansion, keyword search, done. | Owns the orchestration. |
| Thread pool | Fixed worker threads, task queue, resizable on each index build. | Our own implementation. |
| Inverted index | Maps terms to postings; supports concurrent insert and read. | Our own data structure. |
| Indexer | Reads text files, tokenizes, feeds tasks to the pool. | Splits work by document or file batch. |
| Ollama client | Calls `/api/generate`, returns expanded terms, handles timeouts. | Behind an interface for tests. |
| Stats store | Keeps document count, last build time, threads used. | Read by `GET /api/stats`. |
| CLI client | Parses `./search "query"`, calls the server, prints results. | Talks to REST API. |
| Web UI | Search form, results list, stage visualization. | Talks to REST and WebSocket. |
