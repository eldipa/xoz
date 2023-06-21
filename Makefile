.PHONY: all test clean

all:

test:
	tup
	./test/runtests

#-ftime-report -ftime-report-details -H

clean:
	rm -f scratch/mem/*
	rm -f xoz/libxoz.a
	rm -f xoz/repo/*.o
	rm -f xoz/*.o
	rm -f test/*.o
	rm -f test/alltests
