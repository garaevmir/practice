CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2

SRC_CPP = src/bason.cpp src/wal.cpp src/basoncask.cpp
SRC_C = src/sha256.c
OBJ = $(SRC_CPP:.cpp=.o) $(SRC_C:.c=.o)

.PHONY: all clean test

all: test_basoncask

test_basoncask: test/test_basoncask.cpp $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

src/%.o: src/%.c
	$(CC) -Wall -O2 -c -o $@ $<

clean:
	rm -f $(OBJ) test_basoncask

test: test_basoncask
	./test_basoncask
