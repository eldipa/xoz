.PHONY: all test clean

all: test

compile:
	tup 

test: compile
	./test/runtests

coverage:
	lcov  --directory xoz/ --no-external --capture > coverage.info
	genhtml coverage.info

valgrind: compile
	valgrind ./build-default/test/runtests

#-ftime-report -ftime-report-details -H

clean:
	rm -f scratch/mem/*
