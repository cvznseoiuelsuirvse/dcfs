CC = clang

CPPFLAGS = -Ilib/ -Isrc/ 
LDFLAGS = `pkg-config --cflags --libs libcurl openssl fuse3`

SRC = $(wildcard src/*.c) $(wildcard lib/**/*.c)
OBJ = $(SRC:%.c=build/%.o)

TARGET = bin/main

.PHONY: all clean main

all: main
main: $(TARGET)
main: CFLAGS = -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-sign-compare -O0 -g

$(TARGET): $(OBJ)
	mkdir -p $(dir $@)
	$(CC) -o $@ $^ $(LDFLAGS)

build/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $(CPPFLAGS) -o $@ $<


clean:
	rm -rf build/ bin/
