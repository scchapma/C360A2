CC=gcc
CFLAGS=-g -std=c99 -Wall -pedantic
LDFLAGS=-lpthread 

all: PQS

PQS: PQS.c 
	$(CC) $(CFLAGS) PQS.c $(LDFLAGS) -o PQS

clean:
	-rm -rf *.o
