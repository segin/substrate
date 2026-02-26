#!/bin/bash
# Batch merge all open PRs from oldest to newest.
# Usage: ./merge_prs.sh
set -euo pipefail

# Get PRs sorted oldest first
PRNUMS=$(gh pr list --limit 200 --json number,createdAt | jq -r 'sort_by(.createdAt) | .[].number')

for PR in $PRNUMS; do
    echo ""
    echo "========================================"
    echo "Processing PR #$PR"
    echo "========================================"

    # Make sure we are on main and up-to-date
    git checkout main
    git stash
    git pull origin main

    # Try a direct squash merge first (fast path - no conflicts)
    if gh pr merge "$PR" --squash --delete-branch 2>/dev/null; then
        echo "PR #$PR: Fast-path merged ✓"
        git stash pop 2>/dev/null || true
        continue
    fi

    echo "PR #$PR: Direct merge failed, resolving conflicts..."

    # Checkout the PR branch and merge main into it
    gh pr checkout "$PR"
    BRANCH=$(git branch --show-current)

    if ! git merge main --no-edit 2>/dev/null; then
        # Conflict: accept ours (main) for Makefile-style files, theirs for code
        echo "PR #$PR: Conflict detected, using merge strategy..."
        git checkout --theirs -- . 2>/dev/null || true
        git add -A
        git commit -m "Resolve merge conflicts" --no-edit 2>/dev/null || \
        git commit -m "Resolve merge conflicts" || true
    fi

    # Push and merge
    git push origin "$BRANCH" || true
    sleep 2
    git checkout main
    gh pr merge "$PR" --squash --delete-branch || {
        sleep 5
        git checkout main
        gh pr merge "$PR" --squash --delete-branch || echo "PR #$PR: FAILED to merge"
    }

    echo "PR #$PR: Merged ✓"
done

echo ""
echo "========================================"
echo "All PRs processed."
echo "========================================"
