CC = mpicc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lm

SRC_DIR = src
BUILD_DIR = build
BIN_DIR = bin
INC_DIR = include

# Source files
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

# Main executable
EXECUTABLE = $(BIN_DIR)/ffq_mpi

.PHONY: all clean dirs

all: dirs $(EXECUTABLE)

# Create directories
dirs:
	@mkdir -p $(BUILD_DIR) $(BIN_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -I$(INC_DIR) -c $< -o $@

$(EXECUTABLE): $(OBJS)
	$(CC) $^ -o $@ $(LDFLAGS)

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
