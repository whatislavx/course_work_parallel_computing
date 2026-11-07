# Git

- Use Conventional Commits: `type(scope): summary`. Types: `feat`, `fix`, `docs`, `test`, `refactor`, `perf`, `build`, `ci`, `chore`.
- Commits are atomic: one logical change each. The history must show the project growing from the first commit to the final version. A repository with a few large commits is not accepted.
- Summaries are imperative and in English, under 72 characters.
- Work on feature branches named after the task ID (for example `feat/t-012-inverted-index`) and merge to `main` when the task criteria pass.
- Never commit datasets, raw benchmark output, `.env` files, keys, or build artifacts. `.gitignore` covers them; check `git status` before committing.
