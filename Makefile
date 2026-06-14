CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O3
CXXFLAGS += $(shell pkg-config --cflags libusb-1.0)
LDFLAGS  += $(shell pkg-config --libs libusb-1.0) -lz

DIR    = bin
TARGET = $(DIR)/ticli
SRC    = main.cpp

.PHONY: all build clean rebuild install

all: build

build: $(TARGET)

$(TARGET): $(SRC)
	mkdir -p $(DIR)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(TARGET)

rebuild: clean build

install: build
	install -m 755 $(TARGET) /usr/local/bin/ticli