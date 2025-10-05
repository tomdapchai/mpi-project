#ifndef BENCHMARK_MODE_H
#define BENCHMARK_MODE_H

#include <stdbool.h>
#include <stdio.h>
#include <mpi.h>
#include "ffq.h"
#include "weather_data.h"

// Benchmark statistics
typedef struct
{
    double start_time;
    double end_time;
    int items_processed;
    double throughput;
} BenchmarkStats;

// Create a sentinel item to mark the end of the benchmark data
WeatherData create_sentinel_item(void);

// Check if an item is the sentinel
bool is_sentinel_item(const WeatherData *item);

// Ensure the benchmark result directory exists
void ensure_benchmark_dir(void);

// Run benchmark producer - generates simple sequential data for pure queue benchmarking
// NOTE: Currently generates 10000 items in-memory (no file I/O for pure performance testing)
// To use CSV file instead, see commented code in benchmark_mode.c
void run_benchmark_producer(FFQueue *queue, const char *csv_file, int delay_ms,
                            MPI_Win win, BenchmarkStats *stats, int num_consumers, FILE *result_file);

// Run benchmark consumer - processes items concurrently with producer
void run_benchmark_consumer(FFQueue *queue, int consumer_id, int delay_ms,
                            MPI_Win win, BenchmarkStats *stats, FILE *result_file);

#endif // BENCHMARK_MODE_H