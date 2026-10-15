# Desktop build of the same runtime (handy for trying the port on a PC before building the VPK).
#   make          -> ./darkred      (needs SDL2 development files: sdl2-config)
#   make null     -> ./darkred_null (headless test build, no SDL needed)
#   make test     -> run the headless regression tests
#   (PSP: make -f Makefile.psp bundle   Vita: see CMakeLists.txt)
CC      ?= gcc
CFLAGS  ?= -O2 -std=gnu11 -Wall -Wextra -Wno-unused-parameter
SRC      = src/main.c src/pak.c src/vm.c src/rt.c
HDR      = src/gm.h

darkred: $(SRC) src/plat_sdl.c $(HDR)
	$(CC) $(CFLAGS) $(shell sdl2-config --cflags) -o $@ $(SRC) src/plat_sdl.c $(shell sdl2-config --libs) -lm

null: $(SRC) src/plat_null.c $(HDR)
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined -o darkred_null $(SRC) src/plat_null.c -lm

test: null
	sh tests/run_tests.sh

clean:
	rm -f darkred darkred_null
.PHONY: null test clean
