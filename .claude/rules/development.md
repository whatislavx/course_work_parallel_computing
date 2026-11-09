# Development workflow

- One task (GitHub issue TCK-NNN) per change. Reference the task ID in the commit body or PR description.
- Before writing code, confirm the relevant requirement exists in `docs/specification/02-functional-requirements.md` and the API contract in `docs/specification/06-api-specification.md` covers it.
- If a change alters behavior described in the docs, update the docs in the same change.
- Keep the system runnable: `docker compose up` must not break at the end of any task that touches the server.
- Prefer the sequential correct version first. Add the parallel version only after a test proves the sequential one.
