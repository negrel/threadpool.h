CC := clang

CFLAGS := -Wall -Wextra -Wpedantic -Werror -Wnull-dereference -Wformat=2 \
	-Wshadow -Wsign-conversion -Wfloat-equal -Wswitch-enum -Wmissing-prototypes \
	-Wdeprecated-declarations -fsanitize=undefined \
	-fsanitize=thread -std=c11

LDFLAGS := -lpthread

compile_flags.txt:
	echo $(CFLAGS) | tr ' ' '\n' > compile_flags.txt

.PHONY: test
test:
	$(CC) $(CFLAGS) $(LDFLAGS) test.c -o test
	timeout 3s ./test
