# Data flow

## Index build (`POST /api/index`)

1. Client sends `{ "threads": N }`.
2. Indexer lists text files in the dataset directory.
3. Work is split into batches and submitted to the thread pool with N workers.
4. Each worker tokenizes its batch and inserts postings into the shared inverted index through the concurrent structure.
5. When all tasks finish, the server records `documents_indexed` and `time_ms`, and returns them.

## Keyword search (`POST /api/search`, mode `keyword`)

1. Query is tokenized.
2. Index looks up postings for each term and scores documents.
3. Top results are returned with `doc_id`, `score`, `snippet`.

## Pipeline over WebSocket (`/ws/search`)

1. Client sends `{ "query": "..." }`.
2. Server sends `{ "stage": "query_expansion", "data": { "original", "expanded_terms" } }`. If Ollama fails, `expanded_terms` is empty and the failure is noted.
3. Server runs keyword search on the expanded query and sends `{ "stage": "keyword_search", "data": { "results", "time_ms" } }`.
4. Server sends `{ "stage": "done" }`.

Several clients may run pipelines at the same time; each connection has its own pipeline state.
