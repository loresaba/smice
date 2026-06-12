CFLAGS = -Wall -Wextra -O2

all: smice

smice: smice.c
	$(CC) $(CFLAGS) -o smice smice.c

run: smice
	sudo ./smice

clean:
	rm -f smice
