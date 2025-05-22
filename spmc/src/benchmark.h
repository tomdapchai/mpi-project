#ifndef BENCHMARK_H
#define BENCHMARK_H

#include <stdbool.h>
#include "data_types.h"

/**
 * Benchmark modes
 */
typedef enum {
    BENCHMARK_FIXED_TIME,    // Measure number of items processed in fixed time
    BENCHMARK_FIXED_ITEMS    // Measure time to process fixed number of items
} BenchmarkMode;

/**
 * Benchmark configuration
 */
typedef struct {
    BenchmarkMode mode;      // Benchmark mode
    int duration_seconds;    // Duration for fixed time benchmark (seconds)
    int num_items;           // Number of items for fixed item benchmark
    bool detailed_stats;     // Whether to collect detailed statistics
} BenchmarkConfig;

/**
 * Benchmark statistics
 */
typedef struct {
    double total_time_ms;              // Total time in milliseconds
    int items_processed;               // Total items processed
    double throughput;                 // Items per second
    double avg_latency_ms;             // Average processing time per item
    double min_latency_ms;             // Minimum latency
    double max_latency_ms;             // Maximum latency
    double latency_std_dev;            // Standard deviation of latency
} BenchmarkStats;

/**
 * Initialize the benchmark
 * 
 * @param config Benchmark configuration
 */
void benchmark_init(BenchmarkConfig config);

/**
 * Start the benchmark
 */
void benchmark_start();

/**
 * Record a processed item
 * 
 * @param rank Rank of the process
 * @param item_id ID of the processed item
 * @param processing_time_ms Time taken to process the item
 */
void benchmark_record_item(int rank, int item_id, double processing_time_ms);

/**
 * Stop the benchmark
 */
void benchmark_stop();

/**
 * Get the benchmark statistics
 * 
 * @param stats Pointer to fill with statistics
 */
void benchmark_get_stats(BenchmarkStats* stats);

/**
 * Print benchmark results
 */
void benchmark_print_results();

#endif /* BENCHMARK_H */ 