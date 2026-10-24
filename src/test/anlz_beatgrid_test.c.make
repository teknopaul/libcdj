#!/bin/bash

cd $(dirname $0)

test=anlz_beatgrid_test

gcc -Wall -Werror -Wno-unused-function -g -O0 \
    $test.c \
    -o $test -lm \
    && ./$test \
    && rm $test \
    && rm $test.c
