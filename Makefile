.PHONY: all test clean coverage

test-debug: compile-debug
	./build-debug/runtests

test-release: compile-release
	./build-release/runtests

test-relwithdebinfo: compile-relwithdebinfo
	./build-relwithdebinfo/runtests

compile-debug:
	cmake -S . -B ./build-debug -DCMAKE_BUILD_TYPE=Debug $(EXTRA_GENERATE)
	cmake --build  build-debug/ $(EXTRA_COMPILE)

compile-release:
	cmake -S . -B ./build-release -DCMAKE_BUILD_TYPE=Release $(EXTRA_GENERATE)
	cmake --build  build-release/ $(EXTRA_COMPILE)

compile-relwithdebinfo:
	cmake -S . -B ./build-relwithdebinfo -DCMAKE_BUILD_TYPE=RelWithDebInfo $(EXTRA_GENERATE)
	cmake --build  build-relwithdebinfo/ $(EXTRA_COMPILE)

valgrind-debug: compile-debug
	valgrind $(EXTRA_VALGRIND) ./build-debug/runtests

valgrind-release: compile-release
	valgrind $(EXTRA_VALGRIND) ./build-release/runtests

valgrind-relwithdebinfo: compile-relwithdebinfo
	valgrind $(EXTRA_VALGRIND) ./build-relwithdebinfo/runtests

all: debug release relwithdebinfo

gdb-debug:
	gdb -x .gdbinit --args ./build-debug/test/runtests

gdb-release:
	gdb -x .gdbinit --args ./build-release/test/runtests

gdb-relwithdebinfo:
	gdb -x .gdbinit --args ./build-relwithdebinfo/test/runtests

coverage: mirror
	mkdir -p coverage/
	lcov  --directory $(buildvariant)/xoz/ --no-external --capture > coverage/coverage.info
	cd coverage && genhtml coverage.info


#-ftime-report -ftime-report-details -H

mirror:
	find xoz/ test/ \( -name '*.h' -o -name '*.cpp' \) -exec ln -sr {} $(buildvariant)/{} \;

unmirror:
	find $(buildvariant)/xoz/ $(buildvariant)/test/ \( -name '*.h' -o -name '*.cpp' \) -exec rm {} \; || true

clean:
	rm -Rf build-*/
	rm -f scratch/mem/*
	rm -fR coverage/*
	find ./xoz/ ./test/ \( -name '*.o' -o -name '*.gcno' \) -exec rm {} \; || true
