jace: jace.c
	$(CC) $(CFLAGS) $(LDFLAGS) jace.c -o jace -Wall -Wextra -pedantic -std=c99

.PHONY: install
install: jace
	sudo cp jace /usr/local/bin/jace

clean:
	rm jace
