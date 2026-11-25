#!/bin/bash
# usage: mut.sh name 'sed-expr'
cd /home/claude/wt/verify-c2a-src
F=native/core/src/chains/cdiff.cpp
git checkout -q -- $F
sed -i "$2" $F
if git diff --quiet -- $F; then echo "$1: SED DID NOT APPLY"; exit; fi
(cd native/build-v && cmake --build . --target test_chains_diff -j2 2>&1 | grep -E "error|Error" | head -3)
out=$(cd native/build-v && ./tests/test_chains_diff ../tests/golden/chains_diff_cases.json 2>&1 | tail -4)
echo "== $1"; echo "$out" | head -3
git checkout -q -- $F
