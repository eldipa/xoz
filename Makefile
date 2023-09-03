.PHONY: all test clean coverage

all: test

compile:
	tup

test: compile
	./test/runtests

coverage:
	mkdir -p coverage/
	lcov  --directory xoz/ --no-external --capture > coverage/coverage.info
	cd coverage && genhtml coverage.info

valgrind: compile
	valgrind ./build-default/test/runtests

#-ftime-report -ftime-report-details -H

clean:
	rm -f scratch/mem/*
	rm -fR coverage/*
