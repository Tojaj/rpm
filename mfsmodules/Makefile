CC=gcc
CFLAGS= -Wall -Wextra -g -std=c99 -O3 -fpic -shared -I../ -I../include/
LINKFLAGS=

all: testmodule

testmodule:
$(CC) $(CFLAGS) testmodule.c $(LINKFLAGS) -o testmodule.so

clean:
rm -f *.so
