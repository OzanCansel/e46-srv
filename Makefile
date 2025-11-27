.DEFAULT_GOAL := all

CC=gcc

build:
	mkdir build

build/example: build
	mkdir build/example

build/obj:
	mkdir build/obj

build/lib:
	mkdir build/lib

build/obj/e46srv.o: src/include/e46srv.h src/e46srv.c build/obj
	$(CC) -g -I src/include -c src/e46srv.c -o build/obj/e46srv.o

build/lib/libe46srv.a: build/obj/e46srv.o build/lib
	$(AR) rcs build/lib/libe46srv.a build/obj/e46srv.o

lib: build/lib/libe46srv.a

build/example/echo-srv: build/lib/libe46srv.a example/echo-srv/main.c build/example
	$(CC) -g -I src/include example/echo-srv/main.c build/lib/libe46srv.a -o build/example/echo-srv

example: build/example/echo-srv

all: lib example

clean:
	rm -rf build/*