> **Замінено.** Цей документ — рання чернетка. Джерело істини: `docs/specification/` (див. `docs/specification/13-change-log.md`).

# Non-functional requirements

| ID | Requirement | Measure |
|---|---|---|
| NFR-1 | Single-command startup. | `docker compose up` on a clean checkout brings the system to a healthy state. |
| NFR-2 | Race-free concurrent access. | Sanitizer runs report no data races on index build and search. |
| NFR-3 | Dataset size. | At least 1 GB of English text files, at least 10 000 documents. Generated or downloaded per the course instructions. |
| NFR-4 | Scalability is measured. | Index build time vs thread count (1 to N). Throughput vs concurrent clients. |
| NFR-5 | Reproducible experiments. | A script in `benchmarks/` regenerates every plot in the report from the same dataset and seed. |
| NFR-6 | Code quality. | One style guide, formatter applied, no compiler warnings. |
| NFR-7 | Version control. | Git repository on GitHub with Conventional Commits history. |
| NFR-8 | Documentation. | README with build and run instructions; UML diagrams (use case, class, sequence per use case, deployment). |
