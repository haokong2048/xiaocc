#!/bin/bash
set -e

CHIBICC_DIR="/home/ai/chibicc"
XIAOCC_DIR="/home/ai/xiaocc"
ORIGINAL_HEAD="90d1f7f"

# Get commit list
COMMITS=$(cd "$CHIBICC_DIR" && git log --reverse --oneline 310a87e.."$ORIGINAL_HEAD" | awk '{print $1}')
TOTAL=$(echo "$COMMITS" | wc -l)
COUNT=0
FAILED=0

for COMMIT in $COMMITS; do
    COUNT=$((COUNT + 1))

    cd "$CHIBICC_DIR"
    git reset --hard "$COMMIT" > /dev/null 2>&1
    MSG=$(git log --oneline -1 --format='%s' "$COMMIT")
    PARENT=$(git log --oneline -1 --format='%P' "$COMMIT")

    echo ""
    echo "============================================================"
    echo "[$COUNT/$TOTAL] $MSG"
    echo "============================================================"

    # Check what files changed
    CHANGED=$(git diff --name-only "$PARENT".."$COMMIT" 2>/dev/null)
    echo "Changed: $CHANGED"

    # For now, just report and continue
    # The actual diff application needs to be manual for ARM64 adaptation
    echo "DIFF:"
    git diff "$PARENT".."$COMMIT" 2>/dev/null
    echo "---END DIFF---"

    # If there are new test files, copy them
    NEW_TESTS=$(echo "$CHANGED" | grep '^test/' || true)
    if [ -n "$NEW_TESTS" ]; then
        for t in $NEW_TESTS; do
            if [ -f "$CHIBICC_DIR/$t" ]; then
                cp "$CHIBICC_DIR/$t" "$XIAOCC_DIR/$t"
                echo "Copied new test: $t"
            fi
        done
    fi

    # Apply code changes to xiaocc - this is the manual part
    # For now, just show what needs to change
    break  # Stop after first for manual processing
done

echo "Done processing."
