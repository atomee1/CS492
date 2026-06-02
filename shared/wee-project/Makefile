CC=gcc
CFLAGS=-g -Wall -fmessage-length=0 -D_FILE_OFFSET_BITS=64
LIBS=-lfuse

all:
	$(CC) $(CFLAGS) *.c -o fsx492 $(LIBS)

clean:
	rm -f fsx492 *.o *~ core
