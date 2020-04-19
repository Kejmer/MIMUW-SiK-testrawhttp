CC = gcc
CFLAGS = -Wall
TARGETS = testhttp_raw

all: $(TARGETS)

err.o: err.h err.c

testhttp_raw.o: testhttp_raw.c

testhttp_raw: testhttp_raw.o err.o

clean:
	rm -f *.o *~ $(TARGETS)
