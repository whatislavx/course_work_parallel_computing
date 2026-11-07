# Development workflow

- One task from `docs/development/tasks.md` per change. Reference the task ID in the commit body or PR description.
- Before writing code, confirm the relevant requirement exists in `docs/requirements/functional.md` and the API contract in `docs/api/` covers it.
- If a change alters behavior described in the docs, update the docs in the same change.
- Keep the system runnable: `docker compose up` must not break at the end of any task that touches the server.
- Prefer the sequential correct version first. Add the parallel version only after a test proves the sequential one.
