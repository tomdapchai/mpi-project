#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mpi.h>
#include "common.h"
#include "ffq.h"
#include "weather_data.h"
#include "test_mode.h"
#include "file_mode.h"
#include "benchmark_mode.h"

int main(int argc, char** argv) {
    int rank, size;
    MPI_Win win;
    ProgramConfig config;
    
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    
    // Parse command line arguments
    parse_args(argc, argv, &config);
    
    // Print configuration
    if (rank == 0) {
        printf("Configuration:\n");
        printf("  Mode: %s\n", 
               config.mode == TEST_MODE ? "test" : 
               (config.mode == BENCHMARK_MODE ? "benchmark" : "file"));
        printf("  Queue size: %d\n", config.queue_size);
        printf("  Number of items: %d\n", config.num_items);
        printf("  Producer delay: %d ms\n", config.producer_delay_ms);
        printf("  Consumer delay: %d ms\n", config.consumer_delay_ms);
        if (config.mode == FILE_MODE || config.mode == BENCHMARK_MODE) {
            printf("  CSV file: %s\n", config.csv_file);
        }
        printf("  Number of processes: %d\n", size);
    }
    
    // Initialize the queue
    FFQueue* queue = ffq_init(config.queue_size, &win, MPI_COMM_WORLD);
    
    // Run in selected mode
    if (config.mode == TEST_MODE) {
        if (rank == 0) {
            run_producer(queue, config.num_items, config.producer_delay_ms, win);
        } else {
            run_consumer(queue, rank, config.num_items, config.consumer_delay_ms, win);
        }
    } else if (config.mode == FILE_MODE) {
        if (rank == 0) {
            run_file_producer(queue, config.csv_file, config.producer_delay_ms, win);
        } else {
            run_file_consumer(queue, rank, config.consumer_delay_ms, win);
        }
    } else { // BENCHMARK_MODE
        BenchmarkStats stats = {0};
        FILE* result_file = NULL;
        
        // Create benchmark result directory if needed (only by rank 0)
        if (rank == 0) {
            ensure_benchmark_dir();
            
            // Open benchmark result file (overwrite existing)
            result_file = fopen(BENCHMARK_RESULT_FILE, "w");
            if (result_file) {
                fprintf(result_file, "FFQ Benchmark Results\n");
                fprintf(result_file, "====================\n\n");
                fprintf(result_file, "Configuration:\n");
                fprintf(result_file, "  Queue size: %d\n", config.queue_size);
                fprintf(result_file, "  Producer delay: %d ms\n", config.producer_delay_ms);
                fprintf(result_file, "  Consumer delay: %d ms\n", config.consumer_delay_ms);
                fprintf(result_file, "  CSV file: %s\n", config.csv_file);
                fprintf(result_file, "  Number of processes: %d\n", size);
                fprintf(result_file, "  Number of consumers: %d\n\n", size - 1);
            } else {
                printf("Warning: Could not open benchmark result file for writing.\n");
            }
        }
        
        // Just a small synchronization before starting
        MPI_Barrier(MPI_COMM_WORLD);
        
        // Calculate number of consumers (total processes minus producer)
        int num_consumers = size - 1;
        
        // Run benchmark with producer and consumers working concurrently
        if (rank == 0) {
            // Producer process
            run_benchmark_producer(queue, config.csv_file, config.producer_delay_ms, win, &stats, num_consumers, result_file);
        } else {
            // Consumer process
            run_benchmark_consumer(queue, rank, config.consumer_delay_ms, win, &stats, NULL);
        }
        
        // Wait for all processes to finish
        MPI_Barrier(MPI_COMM_WORLD);
        
        // Collect statistics from all processes
        if (rank == 0) {
            BenchmarkStats all_stats[size];
            
            // Gather stats from all processes
            MPI_Gather(&stats, sizeof(BenchmarkStats), MPI_BYTE, 
                      all_stats, sizeof(BenchmarkStats), MPI_BYTE, 
                      0, MPI_COMM_WORLD);
            
            // Calculate overall statistics
            int total_processed = 0;
            double max_end_time = all_stats[0].end_time;
            double min_start_time = all_stats[0].start_time;
            
            for (int i = 1; i < size; i++) {
                total_processed += all_stats[i].items_processed;
                if (all_stats[i].end_time > max_end_time) {
                    max_end_time = all_stats[i].end_time;
                }
                if (all_stats[i].start_time < min_start_time) {
                    min_start_time = all_stats[i].start_time;
                }
            }
            
            // Complete end-to-end time (from producer start to last consumer finish)
            double total_duration = max_end_time - min_start_time;
            
            // Print overall results
            printf("\nBenchmark Results:\n");
            printf("-----------------------------------\n");
            printf("Total items produced: %d\n", all_stats[0].items_processed);
            printf("Total items consumed: %d\n", total_processed);
            printf("Total benchmark time: %.3f seconds\n", total_duration);
            printf("Producer time: %.3f seconds\n", all_stats[0].end_time - all_stats[0].start_time);
            printf("Consumer time (max): %.3f seconds\n", max_end_time - min_start_time);
            
            // Print per-consumer stats
            for (int i = 1; i < size; i++) {
                printf("Consumer %d: %d items, %.2f items/sec, time: %.3f sec\n", 
                       i, all_stats[i].items_processed, all_stats[i].throughput,
                       all_stats[i].end_time - all_stats[i].start_time);
            }
            
            // Calculate and print overall throughput
            double overall_throughput = total_duration > 0 ? 
                all_stats[0].items_processed / total_duration : 0;
                
            printf("\nOverall throughput: %.2f items/second\n", overall_throughput);
            printf("Consumer efficiency: %.1f%%\n", 
                   all_stats[0].items_processed > 0 ? 
                   (total_processed * 100.0 / all_stats[0].items_processed) : 0);
            printf("-----------------------------------\n");
            
            // Write the same information to the result file
            if (result_file) {
                fprintf(result_file, "\nBenchmark Results:\n");
                fprintf(result_file, "-----------------------------------\n");
                fprintf(result_file, "Total items produced: %d\n", all_stats[0].items_processed);
                fprintf(result_file, "Total items consumed: %d\n", total_processed);
                fprintf(result_file, "Total benchmark time: %.3f seconds\n", total_duration);
                fprintf(result_file, "Producer time: %.3f seconds\n", all_stats[0].end_time - all_stats[0].start_time);
                fprintf(result_file, "Consumer time (max): %.3f seconds\n", max_end_time - min_start_time);
                
                // Print per-consumer stats
                for (int i = 1; i < size; i++) {
                    fprintf(result_file, "Consumer %d: %d items, %.2f items/sec, time: %.3f sec\n", 
                           i, all_stats[i].items_processed, all_stats[i].throughput,
                           all_stats[i].end_time - all_stats[i].start_time);
                }
                
                fprintf(result_file, "\nOverall throughput: %.2f items/second\n", overall_throughput);
                fprintf(result_file, "Consumer efficiency: %.1f%%\n", 
                       all_stats[0].items_processed > 0 ? 
                       (total_processed * 100.0 / all_stats[0].items_processed) : 0);
                fprintf(result_file, "-----------------------------------\n");
                
                // Close the result file
                fclose(result_file);
                printf("Benchmark results written to %s\n", BENCHMARK_RESULT_FILE);
            }
        } else {
            // Send stats to rank 0
            MPI_Gather(&stats, sizeof(BenchmarkStats), MPI_BYTE, 
                      NULL, 0, MPI_BYTE, 
                      0, MPI_COMM_WORLD);
        }
    }
    
    // Wait for all processes to finish (except in FILE_MODE)
    if (config.mode != FILE_MODE) {
        MPI_Barrier(MPI_COMM_WORLD);
    } else if (rank == 0) {
        // In FILE_MODE, just wait for user to interrupt
        printf("Press Ctrl+C to stop...\n");
        while(1) {
            do_work(1000);
        }
    }
    
    // Cleanup
    MPI_Win_free(&win);
    MPI_Finalize();
    
    return 0;
}
