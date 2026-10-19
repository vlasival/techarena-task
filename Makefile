CXX ?= g++
CXXFLAGS ?= -O3 -DNDEBUG -std=c++20

.PHONY: build run clean

build: solver

solver: main.cpp
	$(CXX) $(CXXFLAGS) main.cpp -o solver

run:
	./solver

clean:
	rm -f solver
