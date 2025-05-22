#define _POSIX_C_SOURCE 199309L
#include "benchmark.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <mpi.h>

// Benchmark configuration
static BenchmarkConfig config;

// Benchmark runtime data
static bool is_running = false;
static double start_time = 0.0;
static double end_time = 0.0;
static int total_items_processed = 0;
static int* items_per_consumer = NULL;

// For detailed statistics
#define MAX_LATENCIES 100000
static double* latencies = NULL;
static int latency_count = 0;

// Get current time in milliseconds
static double get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

void benchmark_init(BenchmarkConfig cfg) {
    config = cfg;
    is_running = false;
    total_items_processed = 0;
    latency_count = 0;
    
    // Get number of MPI processes
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    
    // Allocate memory for statistics
    if (rank == 0) {
        items_per_consumer = (int*)calloc(size, sizeof(int));
        
        if (config.detailed_stats) {
            int max_items = (config.mode == BENCHMARK_FIXED_ITEMS) ? 
                config.num_items : MAX_LATENCIES;
            latencies = (double*)malloc(max_items * sizeof(double));
        }
    }
    
    // Synchronize all processes
    MPI_Barrier(MPI_COMM_WORLD);
    
    if (rank == 0) {
        printf("Benchmark initialized: %s mode\n", 
               config.mode == BENCHMARK_FIXED_TIME ? "fixed time" : "fixed items");
        
        if (config.mode == BENCHMARK_FIXED_TIME) {
            printf("Duration: %d seconds\n", config.duration_seconds);
        } else {
            printf("Items: %d\n", config.num_items);
        }
    }
}

void benchmark_start() {
    printf("Benchmark start\n"); 
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    
    // Synchronize all processes
    MPI_Barrier(MPI_COMM_WORLD);
    
    is_running = true;
    start_time = get_time_ms();
    
    if (rank == 0) {
        printf("Benchmark started\n");
    }
}

void benchmark_record_item(int rank, int item_id, double processing_time_ms) {
    if (!is_running) {
        return;
    }
    
    // For fixed time mode, check if we've exceeded the duration
    if (config.mode == BENCHMARK_FIXED_TIME) {
        double current_time = get_time_ms();
        if (current_time - start_time > config.duration_seconds * 1000.0) {
            benchmark_stop();
            return;
        }
    }
    
    // Count the item
    MPI_Win win;
    MPI_Win_create(&total_items_processed, sizeof(int), sizeof(int), 
                   MPI_INFO_NULL, MPI_COMM_WORLD, &win);
    
    MPI_Win_lock(MPI_LOCK_EXCLUSIVE, 0, 0, win);
    MPI_Accumulate(&(int){1}, 1, MPI_INT, 0, 0, 1, MPI_INT, MPI_SUM, win);
    MPI_Win_unlock(0, win);
    
    MPI_Win_free(&win);
    
    // Record latency if detailed stats enabled
    if (config.detailed_stats && rank == 0 && latency_count < MAX_LATENCIES) {
        latencies[latency_count++] = processing_time_ms;
    }
    
    // For fixed items mode, check if we've processed all items
    if (config.mode == BENCHMARK_FIXED_ITEMS && rank == 0) {
        if (total_items_processed >= config.num_items) {
            benchmark_stop();
        }
    }
}

void benchmark_stop() {
    if (!is_running) {
        return;
    }
    
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    
    // Record end time
    end_time = get_time_ms();
    is_running = false;
    
    // Synchronize all processes
    MPI_Barrier(MPI_COMM_WORLD);
    
    if (rank == 0) {
        printf("Benchmark stopped\n");
    }
}

void benchmark_get_stats(BenchmarkStats* stats) {
    stats->total_time_ms = end_time - start_time;
    stats->items_processed = total_items_processed;
    stats->throughput = (stats->total_time_ms > 0) ? 
                        (stats->items_processed * 1000.0 / stats->total_time_ms) : 0;
    
    // Calculate latency statistics
    if (config.detailed_stats && latency_count > 0) {
        double sum = 0.0, sum_sq = 0.0;
        stats->min_latency_ms = latencies[0];
        stats->max_latency_ms = latencies[0];
        
        for (int i = 0; i < latency_count; i++) {
            double latency = latencies[i];
            sum += latency;
            sum_sq += latency * latency;
            
            if (latency < stats->min_latency_ms) {
                stats->min_latency_ms = latency;
            }
            if (latency > stats->max_latency_ms) {
                stats->max_latency_ms = latency;
            }
        }
        
        stats->avg_latency_ms = sum / latency_count;
        
        // Standard deviation
        double variance = (sum_sq / latency_count) - (stats->avg_latency_ms * stats->avg_latency_ms);
        stats->latency_std_dev = sqrt(variance);
    } else {
        stats->avg_latency_ms = 0;
        stats->min_latency_ms = 0;
        stats->max_latency_ms = 0;
        stats->latency_std_dev = 0;
    }
}

void benchmark_print_results() {
    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    
    if (rank != 0) {
        return;
    }
    
    BenchmarkStats stats;
    benchmark_get_stats(&stats);
    
    printf("\n=== BENCHMARK RESULTS ===\n");
    printf("Mode: %s\n", config.mode == BENCHMARK_FIXED_TIME ? "Fixed Time" : "Fixed Items");
    printf("Total time: %.2f ms (%.2f seconds)\n", stats.total_time_ms, stats.total_time_ms / 1000.0);
    printf("Items processed: %d\n", stats.items_processed);
    printf("Throughput: %.2f items/second\n", stats.throughput);
    
    if (config.detailed_stats && latency_count > 0) {
        printf("\nLatency statistics:\n");
        printf("  Average: %.2f ms\n", stats.avg_latency_ms);
        printf("  Minimum: %.2f ms\n", stats.min_latency_ms);
        printf("  Maximum: %.2f ms\n", stats.max_latency_ms);
        printf("  Std Dev: %.2f ms\n", stats.latency_std_dev);
    }
    
    printf("=======================\n");
    
    // Clean up
    free(items_per_consumer);
    if (latencies) {
        free(latencies);
    }
} 