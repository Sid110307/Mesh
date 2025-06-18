#!/usr/bin/env bash

if ! command -v cmake >/dev/null 2>&1; then
    echo -e "\033[31m[\033[1;31mERROR\033[31m]: It doesn't look like you have cmake installed. Please install it and try again.\033[0m"
    exit 1
fi

mkdir -p bin
cmake -S . -B bin && cmake --build bin --target run -j4
