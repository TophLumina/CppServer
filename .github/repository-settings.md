# Repository Settings After Disabling CodeQL

This repository no longer carries a dedicated CodeQL workflow.

To fully stop CodeQL on GitHub, update the repository settings in the GitHub web UI after merging this change:

1. Open `Settings` -> `Security & analysis`.
2. Under `Code scanning`, disable any enabled `Default setup` or CodeQL-based analysis.
3. Open `Settings` -> `Rules` or `Branches`, depending on how protection is configured.
4. Edit the ruleset or branch protection that applies to `master` and `main`.
5. Remove `CodeQL` and `Analyze (cpp)` from required status checks.
6. If merge queue, PR templates, or internal release checklists mention CodeQL, remove those references as well.
7. Optionally review existing code scanning alerts and close or dismiss stale alerts if they are no longer tracked.

After this cleanup, the expected repository automation should be limited to the remaining CI workflow in `.github/workflows/ci.yml`.