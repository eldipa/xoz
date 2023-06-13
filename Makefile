.PHONY: all test prepare-scratch build build-test

CXXFLAGS=-std=c++20 -pedantic -Wall -Wextra -Wzero-as-null-pointer-constant -Wconversion -Wno-sign-conversion -Werror -ggdb -O0

GTESTPATH=../googletest/../out/usr/local

GTESTFLAGS=-I${GTESTPATH}/include/ -L${GTESTPATH}/lib/
GTESTLIBS=-lgtest -lpthread


all: build

xoz/libxoz.a: xoz/*.cpp xoz/*.h
	cd xoz/ && g++ ${CXXFLAGS}  -I../ -c *.cpp
	cd xoz/ && ar -rc libxoz.a *.o

build: xoz/libxoz.a

test/alltests: build
	cd test/ && g++ ${CXXFLAGS} ${GTESTFLAGS} -I../ -o alltests *.cpp ../xoz/libxoz.a ${GTESTLIBS}

build-test: test/alltests

test: build-test
	./test/alltests

mount-scratch:
	sudo mount -t tmpfs -o size=10M tmpfs scratch/mem/
	sudo chown user:user scratch/mem/

clean:
	rm -f scratch/mem/*
	rm -f xoz/libxoz.a
	rm -f xoz/*.o
