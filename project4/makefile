CC = gcc
CFLAGS = -Wall -Werror
LDFLAGS = -lpthread

TARGET = main
OBJS = tls.o main.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET) $(OBJS)

tls.o: tls.c tls.h
	$(CC) $(CFLAGS) -c -o tls.o tls.c

main.o: main.c tls.h
	$(CC) $(CFLAGS) -c -o main.o main.c

clean:
	rm -f $(OBJS) $(TARGET)
