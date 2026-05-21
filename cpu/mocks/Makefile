CC=gcc
CFLAGS=-g -Wall -I../../utils/src
OUT=../bin/mock_kernel_memory
SRCS=mock_kernel_memory.c ../../utils/src/utils/sockets.c

.PHONY: all clean

all: $(OUT)

$(OUT): $(SRCS) | ../bin
	$(CC) $(CFLAGS) -o "$@" $(SRCS)

../bin:
	mkdir -p ../bin

clean:
	rm -f $(OUT)
