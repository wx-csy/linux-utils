LAB=malloc

.PHONY: build run clean

build: $(LAB).c test.c
	gcc -std=c99 -O1 -Wall -ggdb -o malloc $^ -lpthread
	g++ -std=c++11 -o check-test check-test.cpp

run: build
	./malloc && ./check-test

clean:
	-rm malloc
	-rm mem.log
	-rm check-test
