CC = mpicc
CFLAGS = -Wall -Wextra -O2
LDFLAGS =

SRC_DIR = src
BUILD_DIR = build
BIN_DIR = bin
INC_DIR = include

# Source files
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

# Main executable
EXECUTABLE = $(BIN_DIR)/ffq_mpi

# Optimized version
OPTIMIZED_EXECUTABLE = $(BIN_DIR)/ffq_mpi_optimized
OPTIMIZED_OBJS = $(BUILD_DIR)/main.o $(BUILD_DIR)/ffq_optimized.o \
                 $(BUILD_DIR)/common.o $(BUILD_DIR)/weather_data.o \
                 $(BUILD_DIR)/test_mode.o $(BUILD_DIR)/file_mode.o \
                 $(BUILD_DIR)/benchmark_mode.o

.PHONY: all clean dirs optimized

all: dirs $(EXECUTABLE)

optimized: dirs $(OPTIMIZED_EXECUTABLE)

# Create directories
dirs:
	@mkdir -p $(BUILD_DIR) $(BIN_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -I$(INC_DIR) -c $< -o $@

$(EXECUTABLE): $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

# Build optimized version
$(OPTIMIZED_EXECUTABLE): $(OPTIMIZED_OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

# Create the folder structure
create_structure:
	@mkdir -p $(SRC_DIR) $(INC_DIR) $(BUILD_DIR) $(BIN_DIR)
	@echo "Project structure created"

# Run the program
run: $(EXECUTABLE)
	mpirun -np 4 $(EXECUTABLE)

# Run benchmark
benchmark: $(EXECUTABLE)
	mpirun -np 4 $(EXECUTABLE) --mode=benchmark

# Run file mode
file: $(EXECUTABLE)
	mpirun -np 4 $(EXECUTABLE) --mode=file

# Run benchmark with options
run_benchmark: $(EXECUTABLE)
	mpirun -np 4 $(EXECUTABLE) --mode=benchmark --producer-delay=0 --consumer-delay=0

# Run optimized benchmark
run_optimized_benchmark: $(OPTIMIZED_EXECUTABLE)
	mpirun -np 4 $(OPTIMIZED_EXECUTABLE) --mode=benchmark --producer-delay=0 --consumer-delay=0

# Compare both versions
compare: $(EXECUTABLE) $(OPTIMIZED_EXECUTABLE)
	@echo "===== Running Original Version ====="
	@time mpirun -np 4 $(EXECUTABLE) --mode=benchmark --producer-delay=0 --consumer-delay=0
	@echo ""
	@echo "===== Running Optimized Version ====="
	@time mpirun -np 4 $(OPTIMIZED_EXECUTABLE) --mode=benchmark --producer-delay=0 --consumer-delay=0

# Sample structure for headers and source files
setup_files: create_structure
	@echo "// FFQ header file" > $(INC_DIR)/ffq.h
	@echo "// FFQ implementation file" > $(SRC_DIR)/ffq.c
	@echo "// Main program file" > $(SRC_DIR)/main.c
	@echo "Basic files created"

# Directory structure:
# - include/  : Header files (*.h)
# - src/      : Source files (*.c)
# - build/    : Object files (*.o)
# - bin/      : Executable files
