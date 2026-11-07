# Implementation plan

Order matters: each step depends on the one before. Each step ends with a passing test.

1. **Skeleton and container.** Server with `GET /api/health`; `docker-compose.yml` with the server and Ollama services. Done when `docker compose up` returns 200 on health.
2. **Sequential index.** Tokenizer, plain inverted index, `POST /api/index` with one thread, `GET /api/stats`. Done when stats match a known test corpus.
3. **Keyword search.** `POST /api/search` with `mode: "keyword"`. Done when results match expected output on the test corpus.
4. **Thread pool.** Own implementation. Index build uses it with the `threads` parameter. Done when the parallel index equals the sequential index for every thread count.
5. **Concurrent index.** Replace the plain structure with the thread-safe one. Done when sanitizer runs are clean.
6. **Ollama integration.** Query expansion through `/api/generate` with fallback. Done when `health.ollama` reflects reachability and expansion returns terms.
7. **WebSocket pipeline.** `/ws/search` with the three stages. Done when a test client receives them in order.
8. **CLI client.** `./search "query"`. Done when output matches REST results.
9. **Web UI.** Form, results, stage visualization over WebSocket. Done when a full search shows all three stages.
10. **Multi-client handling.** Verify concurrent requests; measure throughput.
11. **Benchmarks and report figures.** Scripts in `benchmarks/` regenerate all plots.
