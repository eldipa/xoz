.PHONY: all test clean coverage

all: test

compile: unmirror
	tup

test: compile
	./build-debug/test/runtests

debug:
	gdb -x .gdbinit --args build-debug/test/runtests

coverage: mirror
	mkdir -p coverage/
	lcov  --directory build-debug/xoz/ --no-external --capture > coverage/coverage.info
	cd coverage && genhtml coverage.info

valgrind: compile
	valgrind ./build-debug/test/runtests

#-ftime-report -ftime-report-details -H

mirror:
	find xoz/ test/ \( -name '*.h' -o -name '*.cpp' \) -exec ln -sr {} build-debug/{} \;

unmirror:
	find build-debug/xoz/ build-debug/test/ \( -name '*.h' -o -name '*.cpp' \) -exec rm {} \; || true

clean:
	rm -Rf build-*/
	rm -f scratch/mem/*
	rm -fR coverage/*
	tup variant configs/*.config
