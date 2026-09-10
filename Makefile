CC ?= gcc
CFLAGS = -Wall -Wextra -O3 -pthread

# Håndter OpenSSL-include dynamisk på macOS
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    # Tjek Apple Silicon stien først, derefter Intel/standard Homebrew
    ifneq ($(wildcard /opt/homebrew/opt/openssl@3/include),)
        CFLAGS += -I/opt/homebrew/opt/openssl@3/include
    else ifneq ($(wildcard /usr/local/opt/openssl@3/include),)
        CFLAGS += -I/usr/local/opt/openssl@3/include
    endif
endif

TARGET = server

all: $(TARGET)

$(TARGET): server.c messages.h lonesha256.h
	$(CC) $(CFLAGS) -o $(TARGET) server.c

clean:
	rm -f $(TARGET) *.o