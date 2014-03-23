CC=gcc
#CFLAGS= -Wall -Wextra -g -std=c99 -O3 -fpic -shared -I../ -I../include/
CFLAGS= -Wall -Wextra -g -Og -std=c99 -fpic -shared -I../ -I../include/
LINKFLAGS=

all: testmodule

testmodule:
	$(CC) $(CFLAGS) testmodule.c $(LINKFLAGS) -o testmodule.so

langsubpackagesmodule:
	$(CC) $(CFLAGS) langsubpackagesmodule.c $(LINKFLAGS) -o langsubpackagesmodule.so

debuginfomodule:
	$(CC) $(CFLAGS) debuginfomodule.c $(LINKFLAGS) -o debuginfomodule.so

depgenmodule:
	$(CC) $(CFLAGS) depgenmodule.c $(LINKFLAGS) -o depgenmodule.so

clean:
	rm -f *.so
