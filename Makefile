CXX = g++
CXXFLAGS = -std=c++17 -O3 -march=native -flto -ffast-math -funroll-loops -Wall -Wextra -I include -pthread
LDFLAGS = -pthread -flto

# Source files
SERVER_SRCS = src/server/main_server.cpp \
              src/server/server.cpp \
              src/storage/pager.cpp \
              src/storage/record.cpp \
              src/storage/catalog.cpp \
              src/cache/lru_cache.cpp \
              src/index/bptree.cpp \
              src/parser/parser.cpp \
              src/query/executor.cpp

CLIENT_API_SRCS = src/client/flexql_api.cpp

CLIENT_REPL_SRCS = src/client/repl.cpp

BENCHMARK_SRCS = FlexQL_Benchmark_Unit_Tests-main/benchmark_flexql.cpp

# Object files
SERVER_OBJS = $(SERVER_SRCS:.cpp=.o)
CLIENT_API_OBJS = $(CLIENT_API_SRCS:.cpp=.o)
CLIENT_REPL_OBJS = $(CLIENT_REPL_SRCS:.cpp=.o)

# Targets
all: flexql-server flexql-client libflexql.a benchmark

flexql-server: $(SERVER_OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^

flexql-client: $(CLIENT_REPL_OBJS) $(CLIENT_API_OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^

libflexql.a: $(CLIENT_API_OBJS)
	ar rcs $@ $^

benchmark: $(BENCHMARK_SRCS) $(CLIENT_API_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

# Pattern rule
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	find . -name "*.o" -delete
	rm -f flexql-server flexql-client libflexql.a benchmark
	rm -rf data/

clean-data:
	rm -rf data/

.PHONY: all clean clean-data
