CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -Wpedantic -Werror -g -O3
LDFLAGS = -lrt

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

bench: directories $(BENCH_TARGET)
	@echo "--- Running Zero-Copy vs Single-Copy Benchmark ---"
	@./$(BENCH_TARGET) | tee $(REPORT_DIR)/bench_run.log
	@echo "--- Benchmark Report saved to $(REPORT_DIR)/bench_run.log ---"

clean:
	rm -rf $(BUILD_DIR) $(REPORT_DIR)

.PHONY: all directories clean test bench
