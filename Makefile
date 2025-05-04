CC = gcc
SRC = $(wildcard src/*.c)
OBJ = $(SRC:.c=.o)
TARGET = emu

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) -o $@ $^ -lSDL2

%.o: %.c
	$(CC) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean
