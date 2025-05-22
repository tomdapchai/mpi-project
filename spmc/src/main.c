#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h>
#include <mpi.h>
#include "ffq.h"
#include "gateway.h"
#include "benchmark.h"

#define DEFAULT_QUEUE_SIZE 4
#define DEFAULT_ITEMS 10
#define DEFAULT_PORT 5500

typedef enum {
    TEST_MODE,
    BENCHMARK_MODE,
    STREAM_MODE
} RunMode;

typedef struct {
    int queue_size;
    int num_items;
    RunMode mode;
    int producer_delay_ms;
    int consumer_delay_ms;
    int port;
    BenchmarkConfig benchmark;
} ProgramConfig;

void print_usage(char* program_name) {
    printf("Usage: %s [options]\n", program_name);
    printf("Options:\n");
    printf("  --mode=<test|benchmark|stream>  Run mode (default: test)\n");
    printf("  --queue-size=<size>             Size of the queue (default: %d)\n", DEFAULT_QUEUE_SIZE);
    printf("  --items=<count>                 Number of items to produce (default: %d)\n", DEFAULT_ITEMS);
    printf("  --producer-delay=<ms>           Producer delay in ms (default: 50)\n");
    printf("  --consumer-delay=<ms>           Consumer delay in ms (default: 200)\n");
    printf("  --port=<port>                   Port for gateway in stream mode (default: %d)\n", DEFAULT_PORT);
    printf("  --benchmark-time=<seconds>      Duration for time-based benchmark (default: 60)\n");
    printf("  --detailed-stats                Collect detailed statistics (default: disabled)\n");
    printf("  --help                          Display this help and exit\n");
}

void parse_args(int argc, char** argv, ProgramConfig* config) {
    // Set defaults
    config->queue_size = DEFAULT_QUEUE_SIZE;
    config->num_items = DEFAULT_ITEMS;
    config->mode = TEST_MODE;
    config->producer_delay_ms = 50;
    config->consumer_delay_ms = 200;
    config->port = DEFAULT_PORT;
    
    // Default benchmark config
    config->benchmark.mode = BENCHMARK_FIXED_ITEMS;
    config->benchmark.duration_seconds = 60;
    config->benchmark.num_items = DEFAULT_ITEMS;
    config->benchmark.detailed_stats = false;
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--mode=", 7) == 0) {
            if (strcmp(argv[i] + 7, "benchmark") == 0) {
                config->mode = BENCHMARK_MODE;
            } else if (strcmp(argv[i] + 7, "stream") == 0) {
                config->mode = STREAM_MODE;
            }
        } else if (strncmp(argv[i], "--queue-size=", 13) == 0) {
            config->queue_size = atoi(argv[i] + 13);
        } else if (strncmp(argv[i], "--items=", 8) == 0) {
            config->num_items = atoi(argv[i] + 8);
            config->benchmark.num_items = config->num_items;
        } else if (strncmp(argv[i], "--producer-delay=", 17) == 0) {
            config->producer_delay_ms = atoi(argv[i] + 17);
        } else if (strncmp(argv[i], "--consumer-delay=", 17) == 0) {
            config->consumer_delay_ms = atoi(argv[i] + 17);
        } else if (strncmp(argv[i], "--port=", 7) == 0) {
            config->port = atoi(argv[i] + 7);
        } else if (strncmp(argv[i], "--benchmark-time=", 17) == 0) {
            config->benchmark.mode = BENCHMARK_FIXED_TIME;
            config->benchmark.duration_seconds = atoi(argv[i] + 17);
        } else if (strcmp(argv[i], "--detailed-stats") == 0) {
            config->benchmark.detailed_stats = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            MPI_Abort(MPI_COMM_WORLD, 0);
        } else {
            printf("Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
    }
    
    // Validate arguments
    if (config->queue_size < 2) {
        printf("Queue size must be at least 2\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    
    if (config->num_items < 1) {
        printf("Number of items must be at least 1\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
}

void run_test_producer(FFQueue* queue, int num_items, int delay_ms, MPI_Win win) {
    printf("Test producer started\n");
    
    for (int i = 0; i < num_items; i++) {
        DataItem item;
        item.id = i + 1;
        item.value = i * 1.1;
        item.timestamp = time(NULL);
        snprintf(item.source, sizeof(item.source), "test-producer");
        
        ffq_enqueue(queue, item, win);
        do_work(delay_ms);
    }
    
    printf("Test producer finished\n");
}

void run_stream_producer(FFQueue* queue, int port, int delay_ms, MPI_Win win) {
    printf("Stream producer started on port %d\n", port);
    
    // Initialize and start the gateway
    if (!gateway_init(port)) {
        printf("Failed to initialize gateway\n");
        return;
    }
    
    if (!gateway_start()) {
        printf("Failed to start gateway\n");
        return;
    }
    
    // Process items from the gateway
    DataItem item;
    while (true) {
        if (gateway_get_next(&item)) {
            ffq_enqueue(queue, item, win);
            do_work(delay_ms);
        } else {
            // No item available, wait a bit
            do_work(10);
        }
    }
    
    // Cleanup (should never reach here)
    gateway_shutdown();
}

void run_benchmark_producer(FFQueue* queue, int num_items, int delay_ms, MPI_Win win) {
    printf("Benchmark producer started\n");
    
    benchmark_start();
    
    for (int i = 0; i < num_items; i++) {
        DataItem item;
        item.id = i + 1;
        item.value = i * 1.1;
        item.timestamp = time(NULL);
        snprintf(item.source, sizeof(item.source), "bench-producer");
        
        ffq_enqueue(queue, item, win);
        do_work(delay_ms);
    }
    
    printf("Benchmark producer finished\n");
}

void run_consumer(FFQueue* queue, int consumer_id, int num_items, int delay_ms, MPI_Win win, bool benchmark_mode) {
    printf("Consumer %d started\n", consumer_id);
    
    int items_processed = 0;
    
    while (true) {
        // Check if we should stop
        int lastItem = 0;
        MPI_Win_lock(MPI_LOCK_SHARED, 0, 0, win);
        MPI_Get(&lastItem, 1, MPI_INT, 0, offsetof(FFQueue, lastItemDequeued), 1, MPI_INT, win);
        MPI_Win_flush(0, win);
        MPI_Win_unlock(0, win);
        
        if (lastItem >= num_items) {
            break;
        }
        
        DataItem item;
        double start_time = 0.0;
        
        if (benchmark_mode) {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            start_time = ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
        }
        
        if (ffq_dequeue(queue, consumer_id, &item, win)) {
            // Simulate processing
            do_work(delay_ms);
            items_processed++;
            
            if (benchmark_mode) {
                struct timespec ts;
                clock_gettime(CLOCK_MONOTONIC, &ts);
                double end_time = ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
                double processing_time = end_time - start_time;
                
                benchmark_record_item(consumer_id, item.id, processing_time);
            }
        }
    }
    
    printf("Consumer %d finished, processed %d items\n", consumer_id, items_processed);
}

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
               config.mode == BENCHMARK_MODE ? "benchmark" : "stream");
        printf("  Queue size: %d\n", config.queue_size);
        printf("  Number of items: %d\n", config.num_items);
        printf("  Producer delay: %d ms\n", config.producer_delay_ms);
        printf("  Consumer delay: %d ms\n", config.consumer_delay_ms);
        printf("  Number of processes: %d\n", size);
        
        if (config.mode == STREAM_MODE) {
            printf("  Gateway port: %d\n", config.port);
        }
    }
    
    // Initialize the queue
    FFQueue* queue = ffq_init(config.queue_size, &win, MPI_COMM_WORLD);
    
    // Initialize benchmark if needed
    if (config.mode == BENCHMARK_MODE) {
        benchmark_init(config.benchmark);
    }
    
    // Run in selected mode
    if (rank == 0) {
        // Producer process
        switch (config.mode) {
            case TEST_MODE:
                run_test_producer(queue, config.num_items, config.producer_delay_ms, win);
                break;
                
            case BENCHMARK_MODE:
                run_benchmark_producer(queue, config.num_items, config.producer_delay_ms, win);
                break;
                
            case STREAM_MODE:
                run_stream_producer(queue, config.port, config.producer_delay_ms, win);
                break;
        }
    } else {
        // Consumer process
        run_consumer(queue, rank, config.num_items, config.consumer_delay_ms, win, 
                    config.mode == BENCHMARK_MODE);
    }
    
    // Wait for all processes to finish
    MPI_Barrier(MPI_COMM_WORLD);
    
    // Print benchmark results if needed
    if (config.mode == BENCHMARK_MODE && rank == 0) {
        benchmark_stop();
        benchmark_print_results();
    }
    
    // Cleanup
    MPI_Win_free(&win);
    MPI_Finalize();
    
    return 0;
}
