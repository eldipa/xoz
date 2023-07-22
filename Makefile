.PHONY: all test clean

all: test

test:
	tup
	./build-default/test/runtests
	./build-fuzzing/test/runtests

valgrind:
	tup
	valgrind ./build-default/test/runtests

#-ftime-report -ftime-report-details -H

clean:
	rm -f scratch/mem/*
