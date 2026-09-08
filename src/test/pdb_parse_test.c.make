#!/bin/bash

cd $(dirname $0)

test=pdb_parse_test

gcc -Wall -Werror -Wno-unused-function -g -O0 \
    $test.c \
    -o $test \
    && ./$test \
    && rm $test \
    && rm $test.c
