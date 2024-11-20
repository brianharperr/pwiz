all:
	gcc -O3 src/*.c -Iinclude -o bin/pwiz

clean:
	rm -f bin/main

.PHONY: all clean