jace: jace.c
	$(CC) jace.c -o jace -Wall -Wextra -pedantic -std=c99

.PHONY: install
install: jace
	sudo cp jace /usr/bin/jace

clean:
	rm jace