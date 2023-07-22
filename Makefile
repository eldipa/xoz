.PHONY: all test clean

all:

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
	rm -f xoz/libxoz.a
	rm -f xoz/repo/*.o
	rm -f xoz/alloc/*.o
	rm -f xoz/ext/*.o
	rm -f xoz/*.o
	rm -f test/*.o
	rm -f test/alltests
