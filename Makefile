CC ?= gcc
CFLAGS = -Wall -Wextra -O3 -pthread
LDFLAGS = -pthread

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    ifneq ($(wildcard /opt/homebrew/opt/openssl@3),)
        CFLAGS += -I/opt/homebrew/opt/openssl@3/include
        LDFLAGS += -L/opt/homebrew/opt/openssl@3/lib
    else ifneq ($(wildcard /usr/local/opt/openssl@3),)
        CFLAGS += -I/usr/local/opt/openssl@3/include
        LDFLAGS += -L/usr/local/opt/openssl@3/lib
    endif
endif

TARGET = server

all: $(TARGET)

$(TARGET): server.c messages.h lonesha256.h
	$(CC) $(CFLAGS) -o $(TARGET) server.c $(LDFLAGS) $(LDLIBS)

clean:
	rm -f $(TARGET) *.o