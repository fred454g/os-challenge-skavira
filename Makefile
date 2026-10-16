CC ?= gcc
CFLAGS = -Wall -Wextra -O3 -pthread -Iinclude
LDFLAGS = -pthread

TARGET = server

all: $(TARGET)

$(TARGET): server.c include/messages.h
	$(CC) $(CFLAGS) -o $(TARGET) server.c $(LDFLAGS) $(LDLIBS)

clean:
	rm -f $(TARGET) *.o