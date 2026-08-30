# Repository Instructions

- Use LF line endings for every text file.
- Do not introduce CRLF line endings or stray carriage returns (`^M`).
- When editing on Windows or PowerShell, write files as UTF-8 without BOM and preserve LF. Do not run `unix2dos`.

## Header Organization

- Use `src/sim/Simulation.hpp` as the reference style for non-trivial class headers.
- Group related public APIs with short semantic comments.
- Group private helper methods by responsibility.
- Group member variables by role or domain, such as configuration, runtime state, control, dependencies, and cached data.
- Do not use `#region` or other IDE-specific grouping constructs.
- Do not add grouping comments to tiny classes where they provide no value.
- Avoid redundant comments that only restate member names.
- Reorder declarations only when organizing a header; preserve behavior and public APIs unless the task explicitly requires another refactor.
