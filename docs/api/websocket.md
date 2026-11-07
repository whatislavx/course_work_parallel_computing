# WebSocket: /ws/search

Streams the search pipeline. One request per connection run; each stage is a separate message.

## Client message

```json
{ "query": "search text" }
```

## Server messages (in order)

```json
{ "stage": "query_expansion", "data": { "original": "...", "expanded_terms": ["..."] } }
```

```json
{ "stage": "keyword_search", "data": { "results": [ ... ], "time_ms": 12 } }
```

```json
{ "stage": "done" }
```

## Rules

- Stages arrive in the order above.
- If Ollama fails, `query_expansion` still arrives with `expanded_terms: []`, then the pipeline continues.
- Invalid client messages get an error message and the connection stays open. Format to be agreed; not specified in the assignment.
