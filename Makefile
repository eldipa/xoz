.PHONY: all test clean coverage

builddebugdir ?= build-debug-gcc

all: test

compile: unmirror
	tup

test: compile
	mkdir -p scratch/mem/
	./$(builddebugdir)/test/runtests

debug:
	gdb -x .gdbinit --args $(builddebugdir)/test/runtests

coverage: mirror
	mkdir -p coverage/
	lcov  --directory $(builddebugdir)/xoz/ --no-external --capture > coverage/coverage.info
	cd coverage && genhtml coverage.info

valgrind: compile
	valgrind ./$(builddebugdir)/test/runtests

#-ftime-report -ftime-report-details -H

mirror:
	find xoz/ test/ \( -name '*.h' -o -name '*.cpp' \) -exec ln -sr {} $(builddebugdir)/{} \;

unmirror:
	find $(builddebugdir)/xoz/ $(builddebugdir)/test/ \( -name '*.h' -o -name '*.cpp' \) -exec rm {} \; || true

clean:
	rm -Rf build-*/
	rm -f scratch/mem/*
	rm -fR coverage/*
	tup variant configs/*.config
