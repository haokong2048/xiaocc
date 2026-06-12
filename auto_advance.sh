#!/bin/bash
# 快速自动化处理 chibicc 提交到 xiaocc
set -e

CHIBICC="/home/ai/chibicc"
XIAOCC="/home/ai/xiaocc"
CURRENT="58fc861"  # chibicc 当前已处理位置
FINAL="90d1f7f"    # 最终目标

process_commit() {
    local COMMIT=$1
    cd "$CHIBICC"
    git reset --hard "$COMMIT" > /dev/null 2>&1
    local MSG=$(git log --oneline -1 --format='%s' "$COMMIT")
    local PARENT=$(git log --oneline -1 --format='%P' "$COMMIT")

    echo "=== $MSG ==="

    # Copy new test files
    for f in $(git diff --name-only --diff-filter=A "$PARENT".."$COMMIT" 2>/dev/null | grep '^test/' || true); do
        cp "$CHIBICC/$f" "$XIAOCC/$f"
        echo "  New test: $f"
    done

    # Get changed files
    local CHANGED=$(git diff --name-only "$PARENT".."$COMMIT" 2>/dev/null | grep -v '^test/' || true)
    echo "  Changed: $(echo $CHANGED | tr '\n' ' ')"

    # Show diff for manual processing
    git diff "$PARENT".."$COMMIT" 2>/dev/null
    echo "---END---"
}

cd "$CHIBICC"
COMMITS=$(git log --reverse --oneline "${CURRENT}..${FINAL}" | awk '{print $1}')

for COMMIT in $COMMITS; do
    process_commit "$COMMIT"
    echo ""
    read -p "Press enter to continue..." </dev/tty
done
