#!/bin/bash
# 自动化脚本：将 chibicc 的提交逐笔应用到 xiaocc (ARM64)
set -e

CHIBICC="/home/ai/chibicc"
XIAOCC="/home/ai/xiaocc"
ORIG_HEAD="90d1f7f"  # chibicc 原始 HEAD
START_COMMIT="30b3e21"  # 当前 chibicc 位置（已处理完）

cd "$CHIBICC"
COMMITS=$(git log --reverse --oneline "${START_COMMIT}..${ORIG_HEAD}" | awk '{print $1}')
TOTAL=$(echo "$COMMITS" | wc -l)
COUNT=0

echo "总共 $TOTAL 个提交待处理"

for COMMIT in $COMMITS; do
    COUNT=$((COUNT + 1))

    cd "$CHIBICC"
    git reset --hard "$COMMIT" > /dev/null 2>&1
    MSG=$(git log --oneline -1 --format='%s' "$COMMIT")
    PARENT=$(git log --oneline -1 --format='%P' "$COMMIT")
    CHANGED=$(git diff --name-only "$PARENT".."$COMMIT" 2>/dev/null)

    echo ""
    echo "===== [$COUNT/$TOTAL] $MSG ====="
    echo "Files: $(echo $CHANGED | tr '\n' ' ')"

    cd "$XIAOCC"

    # 复制新的测试文件
    NEW_TESTS=$(echo "$CHANGED" | { grep '^test/' || true; })
    for t in $NEW_TESTS; do
        cp "$CHIBICC/$t" "$XIAOCC/$t"
        echo "  Copied: $t"
    done

    # 生成补丁并应用（chibicc.h -> xiaocc.h 映射）
    cd "$CHIBICC"
    git diff "$PARENT".."$COMMIT" > /tmp/chibicc_diff.patch

    # Fix: chibicc.h -> xiaocc.h
    sed -i 's|chibicc\.h|xiaocc.h|g' /tmp/chibicc_diff.patch

    cd "$XIAOCC"
    if git apply --check /tmp/chibicc_diff.patch 2>/dev/null; then
        git apply /tmp/chibicc_diff.patch
        echo "  Patch applied cleanly"
    else
        echo "  Patch does NOT apply cleanly, showing diff:"
        cat /tmp/chibicc_diff.patch
        echo ""
        echo "  === Manual intervention needed for commit $COMMIT ==="
        echo "  Files changed: $CHANGED"
        exit 1
    fi

    # 编译并测试
    if make clean && make && make test 2>&1 | tail -5 | grep -q 'OK'; then
        echo "  Tests passed!"
        git add -A
        git commit -m "$MSG"
        echo "  Committed: $MSG"
    else
        echo "  TESTS FAILED! Manual intervention needed"
        exit 1
    fi
done

echo ""
echo "===== 所有提交处理完成! ====="
