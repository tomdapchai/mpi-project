#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mpi.h>
#include "ffq.h"

#define DEFAULT_QUEUE_SIZE 4
#define DEFAULT_ITEMS 10

typedef enum {
    TEST_MODE,
    BENCHMARK_MODE
} RunMode;

typedef struct {
    int queue_size;
    int num_items;
    RunMode mode;
    int producer_delay_ms;
    int consumer_delay_ms;
} ProgramConfig;

void print_usage(char* program_name) {
    printf("Usage: %s [options]\n", program_name);
    printf("Options:\n");
    printf("  --mode=<test|benchmark>    Run mode (default: test)\n");
    printf("  --queue-size=<size>        Size of the queue (default: %d)\n", DEFAULT_QUEUE_SIZE);
    printf("  --items=<count>            Number of items to produce (default: %d)\n", DEFAULT_ITEMS);
    printf("  --producer-delay=<ms>      Producer delay in ms (default: 50)\n");
    printf("  --consumer-delay=<ms>      Consumer delay in ms (default: 200)\n");
    printf("  --help                     Display this help and exit\n");
}

void parse_args(int argc, char** argv, ProgramConfig* config) {
    // Set defaults
    config->queue_size = DEFAULT_QUEUE_SIZE;
    config->num_items = DEFAULT_ITEMS;
    config->mode = TEST_MODE;
    config->producer_delay_ms = 50;
    config->consumer_delay_ms = 200;
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--mode=", 7) == 0) {
            if (strcmp(argv[i] + 7, "benchmark") == 0) {
                config->mode = BENCHMARK_MODE;
            }
        } else if (strncmp(argv[i], "--queue-size=", 13) == 0) {
            config->queue_size = atoi(argv[i] + 13);
        } else if (strncmp(argv[i], "--items=", 8) == 0) {
            config->num_items = atoi(argv[i] + 8);
        } else if (strncmp(argv[i], "--producer-delay=", 17) == 0) {
            config->producer_delay_ms = atoi(argv[i] + 17);
        } else if (strncmp(argv[i], "--consumer-delay=", 17) == 0) {
            config->consumer_delay_ms = atoi(argv[i] + 17);
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

void run_producer(FFQueue* queue, int num_items, int delay_ms, MPI_Win win) {
    printf("Producer started\n");
    
    for (int i = 0; i < num_items; i++) {
        int item = i + 1;
        ffq_enqueue(queue, item, win);
        do_work(delay_ms);
    }
    
    printf("Producer finished\n");
}

void run_consumer(FFQueue* queue, int consumer_id, int num_items, int delay_ms, MPI_Win win) {
    printf("Consumer %d started\n", consumer_id);
    
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
        
        int item;
        if (ffq_dequeue(queue, consumer_id, &item, win)) {
            do_work(delay_ms);
        }
    }
    
    printf("Consumer %d finished\n", consumer_id);
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
        printf("  Mode: %s\n", config.mode == TEST_MODE ? "test" : "benchmark");
        printf("  Queue size: %d\n", config.queue_size);
        printf("  Number of items: %d\n", config.num_items);
        printf("  Producer delay: %d ms\n", config.producer_delay_ms);
        printf("  Consumer delay: %d ms\n", config.consumer_delay_ms);
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
    } else { // BENCHMARK_MODE
        // We'll implement benchmarking later
        if (rank == 0) {
            printf("Benchmark mode not yet implemented\n");
        }
    }
    
    // Wait for all processes to finish
    MPI_Barrier(MPI_COMM_WORLD);
    
    // Cleanup
    MPI_Win_free(&win);
    MPI_Finalize();
    
    return 0;
}
