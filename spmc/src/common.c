#include "common.h"

void print_usage(char* program_name) {
    printf("Usage: %s [options]\n", program_name);
    printf("Options:\n");
    printf("  --mode=<test|benchmark|file> Run mode (default: test)\n");
    printf("  --queue-size=<size>          Size of the queue (default: %d)\n", DEFAULT_QUEUE_SIZE);
    printf("  --items=<count>              Number of items to produce (default: %d)\n", DEFAULT_ITEMS);
    printf("  --producer-delay=<ms>        Producer delay in ms (default: 50)\n");
    printf("  --consumer-delay=<ms>        Consumer delay in ms (default: 200)\n");
    printf("  --csv-file=<file>            CSV file to read data from\n");
    printf("  --help                       Display this help and exit\n");
}

void parse_args(int argc, char** argv, ProgramConfig* config) {
    // Set defaults
    config->queue_size = DEFAULT_QUEUE_SIZE;
    config->num_items = DEFAULT_ITEMS;
    config->mode = TEST_MODE;
    config->producer_delay_ms = 50;
    config->consumer_delay_ms = 200;
    strcpy(config->csv_file, "test_data.csv");
    
    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--mode=", 7) == 0) {
            if (strcmp(argv[i] + 7, "benchmark") == 0) {
                config->mode = BENCHMARK_MODE;
                strcpy(config->csv_file, "storage/benchmark.csv");
            } else if (strcmp(argv[i] + 7, "file") == 0) {
                config->mode = FILE_MODE;
            }
        } else if (strncmp(argv[i], "--queue-size=", 13) == 0) {
            config->queue_size = atoi(argv[i] + 13);
        } else if (strncmp(argv[i], "--items=", 8) == 0) {
            config->num_items = atoi(argv[i] + 8);
        } else if (strncmp(argv[i], "--producer-delay=", 17) == 0) {
            config->producer_delay_ms = atoi(argv[i] + 17);
        } else if (strncmp(argv[i], "--consumer-delay=", 17) == 0) {
            config->consumer_delay_ms = atoi(argv[i] + 17);
        } else if (strncmp(argv[i], "--csv-file=", 11) == 0) {
            strncpy(config->csv_file, argv[i] + 11, 255);
            config->csv_file[255] = '\0';
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