CC = gcc
CFLAGS = -std=c99 -Werror
TARGET = myshell

all: $(TARGET)

$(TARGET): myshell.c
	$(CC) $(CFLAGS) -o $(TARGET) myshell.c

clean:
	rm -f $(TARGET)