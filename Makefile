# Feel free to extend this Makefile as you see fit. Just ensure that
# running `make` will compile your code to a binary named `anubis`.

CC = gcc
CFLAGS = -Wall

anubis: anubis.c 
	$(CC) $(CFLAGS) $< -o $@

clean:
	$(RM) anubis 

.PHONY: clean
