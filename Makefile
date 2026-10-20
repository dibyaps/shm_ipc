CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -Wpedantic -Werror -g -O3
LDFLAGS = -lrt

SRC_DIR = src
BUILD_DIR = build

TARGETS = $(BUILD_DIR)/writer $(BUILD_DIR)/reader

all: directories $(TARGETS)

directories:
	@mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/writer: $(SRC_DIR)/shm_writer.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

$(BUILD_DIR)/reader: $(SRC_DIR)/shm_reader.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all directories clean