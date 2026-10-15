#!/bin/sh
# headless test build (no SDL needed)
cd "$(dirname "$0")" && gcc -std=gnu11 -O1 -g -Wall -Wextra -fsanitize=address,undefined -o darkred_null src/main.c src/pak.c src/vm.c src/rt.c src/plat_null.c -lm
