CC ?= gcc
CFLAGS = -Wall -Wextra -O3 -pthread -Iinclude
LDFLAGS = -pthread

TARGET = server

SRCS = server.c src/server_helpers.c
HDRS = include/messages.h include/server_helpers.h include/lonesha256.h

all: $(TARGET)

$(TARGET): $(SRCS) $(HDRS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS) $(LDFLAGS) $(LDLIBS)

clean:
	rm -f $(TARGET) *.o