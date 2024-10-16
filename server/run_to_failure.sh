#!/usr/bin/env bash

# Should be able to run 'forever' or until content with program reliability
# Used to find intermittent/nondeterministic issues with threading

count=$((0))

while "$(dirname "$0")/../full-test.sh"; do
  count=$((count + 1))
  echo "TEST $count COMPLETE"
done
