#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h>
#include <stdbool.h>
#include <mpi.h>
#include <sys/stat.h>
#include <time.h>
#include "ffq.h"
#include "weather_data.h"

#define DEFAULT_QUEUE_SIZE 4
#define DEFAULT_ITEMS 10
#define MAX_LINE_LENGTH 1024

typedef enum {
    TEST_MODE,
    BENCHMARK_MODE,
    FILE_MODE
} RunMode;

typedef struct {
    int queue_size;
    int num_items;
    RunMode mode;
    int producer_delay_ms;
    int consumer_delay_ms;
    char csv_file[256];
} ProgramConfig;

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

// Parse a CSV line into a WeatherData struct
bool parse_csv_line(char* line, WeatherData* data) {
    if (!line || line[0] == '\0' || line[0] == '\n') {
        return false;
    }
    
    // Skip header line
    if (strncmp(line, "timestamp", 9) == 0) {
        return false;
    }
    
    // Format: timestamp,city,aqi,weather_icon,wind_speed,humidity
    char* token;
    
    // Parse timestamp
    token = strtok(line, ",");
    if (!token) return false;
    strncpy(data->timestamp, token, MAX_TIMESTAMP_LEN - 1);
    data->timestamp[MAX_TIMESTAMP_LEN - 1] = '\0';
    
    // Parse city
    token = strtok(NULL, ",");
    if (!token) return false;
    strncpy(data->city, token, MAX_CITY_LEN - 1);
    data->city[MAX_CITY_LEN - 1] = '\0';
    
    // Parse AQI
    token = strtok(NULL, ",");
    if (!token) return false;
    data->aqi = atoi(token);
    
    // Parse weather icon
    token = strtok(NULL, ",");
    if (!token) return false;
    strncpy(data->weather_icon, token, MAX_ICON_LEN - 1);
    data->weather_icon[MAX_ICON_LEN - 1] = '\0';
    
    // Parse wind speed
    token = strtok(NULL, ",");
    if (!token) return false;
    data->wind_speed = atof(token);
    
    // Parse humidity
    token = strtok(NULL, ",");
    if (!token) return false;
    data->humidity = atoi(token);
    
    data->valid = true;
    return true;
}

// Generate test data for TEST_MODE
WeatherData generate_test_data(int item_number) {
    WeatherData data;
    
    sprintf(data.timestamp, "2025-05-23T22:01:56.580965+07:00");
    sprintf(data.city, "TestCity%d", item_number);
    data.aqi = item_number * 10 % 300;
    sprintf(data.weather_icon, "icon%d", item_number % 5);
    data.wind_speed = item_number * 1.5f;
    data.humidity = item_number * 5 % 100;
    data.valid = true;
    
    return data;
}

void run_producer(FFQueue* queue, int num_items, int delay_ms, MPI_Win win) {
    printf("Producer started\n");
    
    for (int i = 0; i < num_items; i++) {
        WeatherData item = generate_test_data(i + 1);
        ffq_enqueue(queue, item, win);
        do_work(delay_ms);
    }
    
    printf("Producer finished\n");
}

void run_file_producer(FFQueue* queue, const char* csv_file, int delay_ms, MPI_Win win) {
    printf("File producer started with file: %s\n", csv_file);
    
    FILE* file = NULL;
    char line[MAX_LINE_LENGTH];
    struct stat file_stat, last_stat;
    long file_pos = 0;
    
    // Initialize stats
    memset(&file_stat, 0, sizeof(file_stat));
    memset(&last_stat, 0, sizeof(last_stat));
    
    while (1) {
        // Check if file exists and get its stats
        if (stat(csv_file, &file_stat) != 0) {
            printf("Cannot stat file %s, waiting...\n", csv_file);
            do_work(1000);
            continue;
        }
        
        // Open file if not already open or if file has been replaced
        if (file == NULL || file_stat.st_ino != last_stat.st_ino) {
            if (file != NULL) {
                fclose(file);
            }
            file = fopen(csv_file, "rb");
            if (file == NULL) {
                printf("Cannot open file %s, waiting...\n", csv_file);
                do_work(1000);
                continue;
            }
            file_pos = 0;
            printf("Opened file %s\n", csv_file);
        }
        
        // Check if file has been modified
        if (file_stat.st_mtime != last_stat.st_mtime || 
            file_stat.st_size != last_stat.st_size) {
            
            // Seek to the last position we read
            fseek(file, file_pos, SEEK_SET);
            
            // Read new data
            while (fgets(line, MAX_LINE_LENGTH, file)) {
                WeatherData data;
                memset(&data, 0, sizeof(WeatherData));
                
                if (parse_csv_line(line, &data)) {
                    ffq_enqueue(queue, data, win);
                    print_weather_data(&data);
                }
                
                file_pos = ftell(file);
                do_work(delay_ms);
            }
            
            // Update last known stats
            last_stat = file_stat;
        } else {
            // No changes, wait a bit
            do_work(500);
        }
    }
    
    // This part will never be reached in this implementation
    if (file != NULL) {
        fclose(file);
    }
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
        
        WeatherData item;
        if (ffq_dequeue(queue, consumer_id, &item, win)) {
            print_weather_data(&item);
            do_work(delay_ms);
        }
    }
    
    printf("Consumer %d finished\n", consumer_id);
}

void run_file_consumer(FFQueue* queue, int consumer_id, int delay_ms, MPI_Win win) {
    printf("File consumer %d started\n", consumer_id);
    
    while (true) {
        WeatherData item;
        if (ffq_dequeue(queue, consumer_id, &item, win)) {
            print_weather_data(&item);
            do_work(delay_ms);
        } else {
            do_work(100); // Small wait if nothing to dequeue
        }
    }
    
    // This part will never be reached in this implementation
    printf("File consumer %d finished\n", consumer_id);
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
               (config.mode == BENCHMARK_MODE ? "benchmark" : "file"));
        printf("  Queue size: %d\n", config.queue_size);
        printf("  Number of items: %d\n", config.num_items);
        printf("  Producer delay: %d ms\n", config.producer_delay_ms);
        printf("  Consumer delay: %d ms\n", config.consumer_delay_ms);
        if (config.mode == FILE_MODE) {
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
        // We'll implement benchmarking later
        if (rank == 0) {
            printf("Benchmark mode not yet implemented\n");
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
