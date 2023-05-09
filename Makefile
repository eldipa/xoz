.PHONY: all test prepare-scratch build build-test

CXXFLAGS=-std=c++2a -pedantic -Wall -Werror -ggdb -O0

GTESTPATH=../googletest/out/usr/local

GTESTFLAGS=-I${GTESTPATH}/include/ -L${GTESTPATH}/lib/
GTESTLIBS=-lgtest -lgtest_main -lpthread


all:

build: xoz/*.cpp xoz/*.h
	cd xoz/ && g++ ${CXXFLAGS}  -I../ -c *.cpp
	cd xoz/ && ar -rc libxoz.a *.o


build-test: build
	cd test/ && g++ ${CXXFLAGS} ${GTESTFLAGS} -I../ *.cpp ../xoz/libxoz.a ${GTESTLIBS}

test: build-test
	./test/a.out

prepare-scratch:
	sudo mount -t tmpfs -o size=10M tmpfs scratch/mem/
	sudo chown user:user scratch/mem/
