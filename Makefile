.PHONY: all test clean

all: test

compile:
	tup

test: compile
	./build-default/test/runtests
	./build-fuzzing/test/runtests

valgrind: compile
	valgrind ./build-default/test/runtests

#-ftime-report -ftime-report-details -H

clean:
	rm -f scratch/mem/*
