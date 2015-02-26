.phony all:
all: PQS

PQS: PQS.c
    gcc PQS.c -o PQS

.PHONY clean:
clean:
    -rm -rf *.o *.exe
