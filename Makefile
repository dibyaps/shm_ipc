CXX = g++

# -----------------------------------------------------------------------------
# Build Profiles:
#   make                -> Release: -O3 -g -DNDEBUG (default, peak low-latency)
#   make BUILD=debug    -> Debug:   -Og -g -DDEBUG   (clean GDB inspection)
#   make BUILD=asan     -> ASan:    -Og -fsanitize=address,undefined (memory safety)
#
# Dynamic Optimization Override:
#   make OPT=-O2        -> Custom optimization level on the fly
# -----------------------------------------------------------------------------
BUILD ?= release

ifeq ($(BUILD), asan)
    OPT_FLAGS ?= -Og -g -DDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer
    EXTRA_LDFLAGS += -fsanitize=address,undefined
else ifeq ($(BUILD), debug)
    OPT_FLAGS ?= -Og -g -DDEBUG
else
    # Default: release
    OPT_FLAGS ?= -O3 -g -DNDEBUG
endif

# Allow user to dynamically override OPT flag (e.g. make OPT=-O2 or make OPT=-O1)
ifdef OPT
    OPT_FLAGS = $(OPT) -g
endif

CXXFLAGS = -std=c++20 -Wall -Wextra -Wpedantic -Werror $(OPT_FLAGS)
LDFLAGS = -lrt $(EXTRA_LDFLAGS)

SRC_DIR = src
TEST_DIR = tests
BENCH_DIR = benchmarks
BUILD_DIR = build
REPORT_DIR = reports

TARGETS = $(BUILD_DIR)/writer $(BUILD_DIR)/reader
TEST_TARGET = $(BUILD_DIR)/unit_tests
BENCH_TARGET = $(BUILD_DIR)/bench_zerocopy

# All test sources combined into the test binary
TEST_SRCS = $(TEST_DIR)/test_layout.cpp \
            $(TEST_DIR)/test_shm_region.cpp \
            $(TEST_DIR)/test_ring_buffer.cpp \
            $(TEST_DIR)/test_sdk.cpp

all: directories $(TARGETS) $(BENCH_TARGET)

directories:
	@mkdir -p $(BUILD_DIR) $(REPORT_DIR)

# Production binaries
$(BUILD_DIR)/writer: $(SRC_DIR)/shm_writer.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

$(BUILD_DIR)/reader: $(SRC_DIR)/shm_reader.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

# Benchmark binary
$(BENCH_TARGET): $(BENCH_DIR)/bench_zerocopy.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

# Google Test unified runner
$(TEST_TARGET): $(TEST_SRCS)
	$(CXX) $(CXXFLAGS) $(TEST_SRCS) -o $@ $(LDFLAGS) -lgtest -lgtest_main -lpthread

test: directories $(TEST_TARGET)
	@echo "--- Running All Google Unit & Integration Tests ---"
	@./$(TEST_TARGET) --gtest_output="xml:$(REPORT_DIR)/test_report.xml" | tee $(REPORT_DIR)/test_run.log
	@echo "--- Test Report saved to $(REPORT_DIR)/test_report.xml and $(REPORT_DIR)/test_run.log ---"
	@python3 scripts/generate_report.py

bench: directories $(BENCH_TARGET)
	@echo "--- Running Zero-Copy vs Single-Copy Benchmark ---"
	@./$(BENCH_TARGET) | tee $(REPORT_DIR)/bench_run.log
	@echo "--- Benchmark Report saved to $(REPORT_DIR)/bench_run.log ---"

clean:
	rm -rf $(BUILD_DIR) $(REPORT_DIR)

.PHONY: all directories clean test bench
