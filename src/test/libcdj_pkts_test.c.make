#!/bin/bash
set -e

cd $(dirname $0)

#prof="-fprofile-arcs -ftest-coverage"

test=libcdj_pkts_test

gcc $prof -Wall -Werror -c dump.c
gcc $prof -Wall -Werror -Wno-unused-function -c $test.c
gcc $prof -Wall -Werror -Wno-unused-function -g -O0 \
    dump.o $test.o ../../target/cdj.o \
    -o $test \
    && ./$test \
    && rm $test \
    && rm $test.c
