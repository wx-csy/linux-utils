# Do *NOT* modify the existing build rules.
# You may add your own rules, e.g., "make run" or "make test".

LAB = pstree

.PHONY: build

build: $(LAB).c
	mkdir -p build
	gcc -std=c99 -O1 -Wall -ggdb -o build/$(LAB) $(LAB).c

