# Code quality

- Follow one style guide across the whole project (Google Style Guide for the chosen language, per the assignment). Enforce it with a formatter in `scripts/` or CI before the first milestone.
- Clean code: short functions, meaningful names, no commented-out code, no dead code.
- No global mutable state outside the index and thread pool.
- Errors are reported with a status and a message. No silent catches.
- Warnings are errors in the build configuration.
