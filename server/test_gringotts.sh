#!/bin/zsh

echo "Should Succeed"
./thealgo < tests/test0
echo ""
echo "Should Fail"
./thealgo < tests/test1
