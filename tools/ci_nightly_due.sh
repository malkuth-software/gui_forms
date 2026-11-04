#!/bin/sh
# Whether a scheduled native.yml run has anything new to check: writes
# run=false to $GITHUB_OUTPUT when the last successful scheduled run checked
# this same commit ($GITHUB_SHA), except on Sundays; run=true otherwise,
# including when the lookup fails. Needs GH_TOKEN and GH_REPO.
# Outside GitHub Actions (no $GITHUB_OUTPUT) there is nothing to decide and
# it exits 0, so local runners of the workflow's commands pass over it.
set -u
if [ -z "${GITHUB_OUTPUT:-}" ]; then
    echo "ci_nightly_due: not in GitHub Actions; nothing to decide"
    exit 0
fi
run=true
if [ "$(date -u +%u)" != 7 ]; then
    last=$(gh api "repos/$GH_REPO/actions/workflows/native.yml/runs?event=schedule&status=success&per_page=1" \
        --jq '.workflow_runs[0].head_sha // ""' || true)
    if [ -n "$last" ] && [ "$last" = "$GITHUB_SHA" ]; then
        run=false
        echo "The last successful scheduled run checked $GITHUB_SHA already; nothing runs until main changes or Sunday"
    fi
fi
echo "run=$run" >> "$GITHUB_OUTPUT"
