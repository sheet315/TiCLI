CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra

CXXFLAGS += $(shell pkg-config --cflags libusb-1.0)
LDFLAGS  += $(shell pkg-config --libs libusb-1.0)

DIR = bin
TARGET = $(DIR)/ticli
SRC = main.cpp

all: build

build:
	mkdir -p $(DIR)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS) -O3

clean:
	rm -f $(TARGET)

cleanbuild:
	rm -f $(TARGET)
	mkdir -p $(DIR)
	$(CXX) $(CXXFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS) -O3