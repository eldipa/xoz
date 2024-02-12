.PHONY: all test clean coverage

ifndef buildvariant
compileallvariants ?= 1
buildvariant = build-debug-gcc
else
compileallvariants ?= 0
endif

all: test

compile: unmirror
	if [ "$(compileallvariants)" = "1" ]; then tup; else tup $(buildvariant); fi

test: compile
	mkdir -p scratch/mem/
	./$(buildvariant)/test/runtests

debug:
	gdb -x .gdbinit --args $(buildvariant)/test/runtests

coverage: mirror
	mkdir -p coverage/
	lcov  --directory $(buildvariant)/xoz/ --no-external --capture > coverage/coverage.info
	cd coverage && genhtml coverage.info

valgrind: compile
	valgrind ./$(buildvariant)/test/runtests

#-ftime-report -ftime-report-details -H

mirror:
	find xoz/ test/ \( -name '*.h' -o -name '*.cpp' \) -exec ln -sr {} $(buildvariant)/{} \;

unmirror:
	find $(buildvariant)/xoz/ $(buildvariant)/test/ \( -name '*.h' -o -name '*.cpp' \) -exec rm {} \; || true

clean:
	rm -Rf build-*/
	rm -f scratch/mem/*
	rm -fR coverage/*
	tup variant configs/*.config
