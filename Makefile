CC := gcc
CC_ARGS := -Wall -Wextra -Werror -pedantic -O0 -ggdb3 -std=gnu11 -m64 -v
LD_ARGS := -Wl,-v

build:
	$(CC) $(CC_ARGS) $(LD_ARGS) cycles.c -o cycles

clean:
	rm ./cycles

.PHONY: build clean
