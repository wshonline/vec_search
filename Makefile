CXX = g++

CXXFLAGS += -std=c++17 -O3 -fPIC -g -ffp-contract=off -march=native -fopenmp
LINK_FLAGS = -lpmem -lpmemobj -pthread -Wl,-rpath,/usr/local/lib:/usr/local/lib64:/usr/lib:/usr/lib64

IMPL_DIR = impl
TEST_DIR = test
INCLUDE_DIR = include

all: demo unittest

demo: demo.cpp
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) -I$(IMPL_DIR) $^ -o $@ $(LINK_FLAGS)

unittest: $(TEST_DIR)/*.cpp
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) -I$(IMPL_DIR) $^ -o $@ $(LINK_FLAGS) -lgtest_main -lgtest

clean:
	rm -f demo unittest > /dev/null 2>&1
