# REST API

Fixed contract from Appendix A of the assignment. All responses are JSON. Changing a field requires updating this file and the assignment's agreement.

Base path: `/api`

## GET /api/health

Response `200`:

```json
{ "status": "ok", "ollama": true }
```

`ollama` is `false` when the Ollama service cannot be reached. The endpoint still returns `200`.

## POST /api/index

Request:

```json
{ "threads": 4 }
```

Response `200`:

```json
{ "status": "ok", "documents_indexed": 2000, "time_ms": 1234 }
```

## POST /api/search

Request:

```json
{ "query": "text", "mode": "keyword" }
```

Response `200`:

```json
{
  "results": [
    { "doc_id": "...", "score": 0.95, "snippet": "..." }
  ],
  "time_ms": 56
}
```

## GET /api/stats

Response `200`:

```json
{ "documents": 2000, "index_build_time_ms": 1234, "threads_used": 4 }
```

## Errors

Not specified in the assignment. Proposed: `400` for malformed JSON or unknown `mode`, `409` for search before any index is built, `500` for internal errors. Body: `{ "error": "message" }`. Confirm before implementing.
