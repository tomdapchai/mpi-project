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

// Special identifier for sentinel value
#define SENTINEL_CITY "##BENCHMARK_END##"
#define BENCHMARK_RESULT_FILE "benchmark_result/benchmark.txt"

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

// Benchmark statistics
typedef struct {
    double start_time;
    double end_time;
    int items_processed;
    double throughput;
} BenchmarkStats;

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

// Create a sentinel item to mark the end of the benchmark data
WeatherData create_sentinel_item() {
    WeatherData sentinel;
    memset(&sentinel, 0, sizeof(WeatherData));
    
    // Special timestamp
    strcpy(sentinel.timestamp, "9999-12-31T23:59:59.999999+00:00");
    
    // Use a special city name as the marker
    strcpy(sentinel.city, SENTINEL_CITY);
    
    // Other fields
    sentinel.aqi = -1;
    strcpy(sentinel.weather_icon, "none");
    sentinel.wind_speed = -1.0;
    sentinel.humidity = -1;
    sentinel.valid = true;
    
    return sentinel;
}

// Check if an item is the sentinel
bool is_sentinel_item(const WeatherData* item) {
    return (item != NULL && 
            item->valid && 
            strcmp(item->city, SENTINEL_CITY) == 0);
}

// Ensure the benchmark result directory exists
void ensure_benchmark_dir() {
    struct stat st = {0};
    
    // Check if the directory exists
    if (stat("benchmark_result", &st) == -1) {
        // Create the directory if it doesn't exist
        mkdir("benchmark_result", 0700);
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

// Run benchmark producer - reads from file until EOF, with consumers working concurrently
void run_benchmark_producer(FFQueue* queue, const char* csv_file, int delay_ms, MPI_Win win, BenchmarkStats* stats, int num_consumers, FILE* result_file) {
    printf("Benchmark producer started with file: %s\n", csv_file);
    if (result_file) {
        fprintf(result_file, "Benchmark producer started with file: %s\n", csv_file);
    }
    
    stats->start_time = MPI_Wtime();
    stats->items_processed = 0;
    
    FILE* file = fopen(csv_file, "rb");
    if (file == NULL) {
        printf("Cannot open benchmark file %s\n", csv_file);
        if (result_file) {
            fprintf(result_file, "Cannot open benchmark file %s\n", csv_file);
        }
        
        // Signal that producer is done (with 0 items)
        int producer_done = 1;
        MPI_Win_lock(MPI_LOCK_EXCLUSIVE, 0, 0, win);
        MPI_Put(&producer_done, 1, MPI_INT, 0, 
                offsetof(FFQueue, lastItemDequeued), 
                1, MPI_INT, win);
        MPI_Win_flush(0, win);
        MPI_Win_unlock(0, win);
        stats->end_time = MPI_Wtime();
        stats->throughput = 0;
        return;
    }
    
    char line[MAX_LINE_LENGTH];
    
    // Skip header line
    if (fgets(line, MAX_LINE_LENGTH, file) == NULL) {
        printf("Empty benchmark file\n");
        if (result_file) {
            fprintf(result_file, "Empty benchmark file\n");
        }
        
        fclose(file);
        // Signal that producer is done (with 0 items)
        int producer_done = 1;
        MPI_Win_lock(MPI_LOCK_EXCLUSIVE, 0, 0, win);
        MPI_Put(&producer_done, 1, MPI_INT, 0, 
                offsetof(FFQueue, lastItemDequeued), 
                1, MPI_INT, win);
        MPI_Win_flush(0, win);
        MPI_Win_unlock(0, win);
        stats->end_time = MPI_Wtime();
        stats->throughput = 0;
        return;
    }
    
    // Read and enqueue all data from the file
    while (fgets(line, MAX_LINE_LENGTH, file)) {
        WeatherData data;
        memset(&data, 0, sizeof(WeatherData));
        
        if (parse_csv_line(line, &data)) {
            ffq_enqueue(queue, data, win);
            stats->items_processed++;
            
            if (stats->items_processed % 100 == 0) {
                printf("Enqueued %d items...\n", stats->items_processed);
                if (result_file) {
                    fprintf(result_file, "Enqueued %d items...\n", stats->items_processed);
                }
            }
            
            // Optional delay between items
            if (delay_ms > 0) {
                do_work(delay_ms);
            }
        }
    }
    
    // Add sentinel values - one for each consumer
    WeatherData sentinel = create_sentinel_item();
    for (int i = 0; i < num_consumers; i++) {
        ffq_enqueue(queue, sentinel, win);
    }
    printf("Enqueued %d sentinel items - one for each consumer\n", num_consumers);
    if (result_file) {
        fprintf(result_file, "Enqueued %d sentinel items - one for each consumer\n", num_consumers);
    }
    
    // Signal that producer is done
    int producer_done = 1;
    int total_items = stats->items_processed;
    
    MPI_Win_lock(MPI_LOCK_EXCLUSIVE, 0, 0, win);
    // Store the total number of items in the queue's lastItemDequeued field
    // This serves as a flag to consumers that producer is done
    MPI_Put(&total_items, 1, MPI_INT, 0, 
            offsetof(FFQueue, lastItemDequeued), 
            1, MPI_INT, win);
    MPI_Win_flush(0, win);
    MPI_Win_unlock(0, win);
    
    stats->end_time = MPI_Wtime();
    double duration = stats->end_time - stats->start_time;
    stats->throughput = duration > 0 ? stats->items_processed / duration : 0;
    
    printf("Benchmark producer finished:\n");
    printf("  Items enqueued: %d\n", stats->items_processed);
    printf("  Total time: %.3f seconds\n", duration);
    printf("  Enqueue rate: %.2f items/second\n", stats->throughput);
    
    if (result_file) {
        fprintf(result_file, "Benchmark producer finished:\n");
        fprintf(result_file, "  Items enqueued: %d\n", stats->items_processed);
        fprintf(result_file, "  Total time: %.3f seconds\n", duration);
        fprintf(result_file, "  Enqueue rate: %.2f items/second\n", stats->throughput);
    }
    
    fclose(file);
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

// Run benchmark consumer - processes items concurrently with producer
void run_benchmark_consumer(FFQueue* queue, int consumer_id, int delay_ms, MPI_Win win, BenchmarkStats* stats, FILE* result_file) {
    printf("Benchmark consumer %d started\n", consumer_id);
    if (result_file) {
        fprintf(result_file, "Benchmark consumer %d started\n", consumer_id);
    }
    
    stats->start_time = MPI_Wtime();
    stats->items_processed = 0;
    
    bool found_sentinel = false;
    
    while (!found_sentinel) {
        // Try to dequeue an item
        WeatherData item;
        if (ffq_dequeue(queue, consumer_id, &item, win)) {
            // Check if this is the sentinel
            if (is_sentinel_item(&item)) {
                printf("Consumer %d found sentinel, benchmark complete\n", consumer_id);
                if (result_file) {
                    fprintf(result_file, "Consumer %d found sentinel, benchmark complete\n", consumer_id);
                }
                found_sentinel = true;
                break;
            }
            
            // Process the item
            stats->items_processed++;
            
            if (stats->items_processed % 100 == 0) {
                printf("Consumer %d processed %d items...\n", consumer_id, stats->items_processed);
                if (result_file) {
                    fprintf(result_file, "Consumer %d processed %d items...\n", consumer_id, stats->items_processed);
                }
            }
            
            // Optional processing delay
            if (delay_ms > 0) {
                do_work(delay_ms);
            }
        } else {
            // Small wait if nothing to dequeue
            do_work(10);
        }
    }
    
    stats->end_time = MPI_Wtime();
    double duration = stats->end_time - stats->start_time;
    stats->throughput = duration > 0 ? stats->items_processed / duration : 0;
    
    printf("Benchmark consumer %d finished:\n", consumer_id);
    printf("  Items processed: %d\n", stats->items_processed);
    printf("  Processing time: %.3f seconds\n", duration);
    printf("  Processing rate: %.2f items/second\n", stats->throughput);
    
    if (result_file) {
        fprintf(result_file, "Benchmark consumer %d finished:\n", consumer_id);
        fprintf(result_file, "  Items processed: %d\n", stats->items_processed);
        fprintf(result_file, "  Processing time: %.3f seconds\n", duration);
        fprintf(result_file, "  Processing rate: %.2f items/second\n", stats->throughput);
    }
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
