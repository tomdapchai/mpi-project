#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h>
#include <stdbool.h>
#include <mpi.h>
#include <sys/stat.h>
#include <time.h>

#define DEFAULT_QUEUE_SIZE 4
#define DEFAULT_ITEMS 10
#define MAX_LINE_LENGTH 1024

// Special identifier for sentinel value
#define SENTINEL_CITY "##BENCHMARK_END##"
#define BENCHMARK_RESULT_FILE "benchmark_result/benchmark.txt"

typedef enum
{
    TEST_MODE,
    BENCHMARK_MODE,
    FILE_MODE
} RunMode;

typedef struct
{
    int queue_size;
    int num_items;
    RunMode mode;
    int producer_delay_ms;
    int consumer_delay_ms;
    char csv_file[256];
} ProgramConfig;

// Print usage information
void print_usage(char *program_name);

// Parse command line arguments
void parse_args(int argc, char **argv, ProgramConfig *config);

#endif // COMMON_H