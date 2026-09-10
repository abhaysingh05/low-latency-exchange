CXX      := g++
CXXFLAGS := -std=c++20 -O3 -march=native -flto -Wall -Wextra -Wpedantic \
            -Iinclude
LDFLAGS  := -lpthread

SRC_DIR   := src
BENCH_DIR := bench
TEST_DIR  := tests
BUILD_DIR := build

# Source files (excluding main for library objects)
LIB_SRCS  := $(SRC_DIR)/order_book.cpp $(SRC_DIR)/matching_engine.cpp $(SRC_DIR)/tcp_server.cpp
LIB_OBJS  := $(LIB_SRCS:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)
MAIN_OBJ  := $(BUILD_DIR)/main.o

# ============================================================
# Targets
# ============================================================
.PHONY: all exchange bench test clean

all: exchange bench-bins test-bin

# --- Exchange binary ---
exchange: $(BUILD_DIR)/exchange
	@echo "Built: $(BUILD_DIR)/exchange"

$(BUILD_DIR)/exchange: $(LIB_OBJS) $(MAIN_OBJ) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

# --- Object files ---
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# --- Benchmarks ---
bench-bins: $(BUILD_DIR)/bench_order_book $(BUILD_DIR)/bench_throughput
	@echo "Built benchmarks"

$(BUILD_DIR)/bench_order_book: $(BENCH_DIR)/bench_order_book.cpp $(BUILD_DIR)/order_book.o | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(BENCH_DIR) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/bench_throughput: $(BENCH_DIR)/bench_throughput.cpp \
    $(BUILD_DIR)/order_book.o $(BUILD_DIR)/matching_engine.o | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(BENCH_DIR) $^ -o $@ $(LDFLAGS)

bench: bench-bins
	@echo ""
	@echo "Running order book benchmarks..."
	$(BUILD_DIR)/bench_order_book
	@echo ""
	@echo "Running throughput benchmarks..."
	$(BUILD_DIR)/bench_throughput

# --- Tests ---
test-bin: $(BUILD_DIR)/test_all
	@echo "Built tests"

$(BUILD_DIR)/test_all: $(TEST_DIR)/test_all.cpp $(BUILD_DIR)/order_book.o | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

test: test-bin
	$(BUILD_DIR)/test_all

# --- Build directory ---
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# --- Clean ---
clean:
	rm -rf $(BUILD_DIR)
