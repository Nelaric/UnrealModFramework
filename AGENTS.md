# Agent Instructions

## Pull Requests

Prefer fewer pull requests by keeping related changes for the same feature or fix together. Split work when the resulting pull requests cover distinct units that can be reviewed and verified independently.

When submitting multiple pull requests together, base each PR directly on the same target branch. Each PR must be self-contained and verifiable without changes from any other PR in the batch. The PRs must touch disjoint sets of files, counting both the old and new paths of any renamed file.

Before pushing any PR branch in the batch, verify that all PRs can be merged into the target branch in any order in a single pass without conflicts, rebases, or manual fixes between merges.

## Tests

Do not create new tests, test cases, or test files unless the user explicitly requests them. Existing tests may be run to verify changes.

## Temporary Builds

Give each run a unique temporary build directory under the system temp directory (`$env:TEMP` on Windows) or the repository's ignored `.tools/tmp/` directory. Use `.tools/tmp/` if the system temp path resolves to a drive root. When reusing an SDK build request, update its `workDirectory` for the new run. Omit that field if the SDK tool's system temp default follows this rule.

Save requested deliverables separately. After the build and any verification processes exit, remove the entire temporary directory created for the run, including on failure. In PowerShell, use `finally` and verify the resolved path before deleting it.
