EXEC = httpfs

CFLAGS  = -W -Wall -O2 -g -pipe -ansi -pedantic -std=gnu99 -D_FILE_OFFSET_BITS=64 -DFUSE_USE_VERSION=22 -I/usr/include/libxml2
LDFLAGS = -lfuse -lcurl -lxml2

CC = gcc
