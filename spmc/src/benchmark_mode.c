#include "benchmark_mode.h"
#include "common.h"
#include "file_mode.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

// ===== BENCHMARK CONFIGURATION =====
// Number of items to generate for benchmarking (adjust as needed)
// This eliminates file I/O overhead and focuses purely on queue performance
#define BENCHMARK_ITEMS 10000

// To use CSV file instead of generated data, see commented code in run_benchmark_producer()

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

// Run benchmark producer - generates simple sequential data for pure queue benchmarking
void run_benchmark_producer(FFQueue* queue, const char* csv_file, int delay_ms, MPI_Win win, BenchmarkStats* stats, int num_consumers, FILE* result_file) {
    printf("Benchmark producer started (generating %d items)\n", BENCHMARK_ITEMS);
    if (result_file) {
        fprintf(result_file, "Benchmark producer started (generating %d items)\n", BENCHMARK_ITEMS);
    }
    
    stats->start_time = MPI_Wtime();
    stats->items_processed = 0;
    
    // ===== PURE BENCHMARK MODE: Generate simple sequential data =====
    // This eliminates file I/O overhead and focuses on queue performance
    
    /* OPTION 1: Minimal overhead - fastest (uncomment to use)
    for (int i = 1; i <= BENCHMARK_ITEMS; i++) {
        WeatherData data;
        memset(&data, 0, sizeof(WeatherData));
        
        // Absolute minimal - just integer data, no string formatting
        data.aqi = i;
        data.wind_speed = (float)i;
        data.humidity = i % 100;
        data.valid = true;
        // Leave strings empty (already zeroed by memset)
        
        ffq_enqueue(queue, data, win);
        stats->items_processed++;
        
        if (stats->items_processed % 1000 == 0) {
            printf("Enqueued %d items...\n", stats->items_processed);
        }
        
        if (delay_ms > 0) {
            do_work(delay_ms);
        }
    }
    */
    
    // OPTION 2: Simple formatted data (current - balanced approach)
    for (int i = 1; i <= BENCHMARK_ITEMS; i++) {
        WeatherData data;
        memset(&data, 0, sizeof(WeatherData));
        
        // Simple sequential data - just populate with item number
        snprintf(data.timestamp, MAX_TIMESTAMP_LEN, "Item-%d", i);
        snprintf(data.city, MAX_CITY_LEN, "City-%d", i % 100);  // Cycle through 100 cities
        data.aqi = i % 500;  // AQI between 0-499
        snprintf(data.weather_icon, MAX_ICON_LEN, "Icon-%d", i % 10);
        data.wind_speed = (float)(i % 100);
        data.humidity = i % 100;
        data.valid = true;
        
        ffq_enqueue(queue, data, win);
        stats->items_processed++;
        
        if (stats->items_processed % 1000 == 0) {
            printf("Enqueued %d items...\n", stats->items_processed);
        }
        
        // Optional delay between items
        if (delay_ms > 0) {
            do_work(delay_ms);
        }
    }
    
    /*
    // ===== OLD FILE-BASED MODE (commented out) =====
    // Uncomment this section if you want to read from CSV file instead
    
    FILE* file = fopen(csv_file, "rb");
    if (file == NULL) {
        printf("Cannot open benchmark file %s\n", csv_file);
        stats->end_time = MPI_Wtime();
        stats->throughput = 0;
        return;
    }
    
    char line[MAX_LINE_LENGTH];
    if (fgets(line, MAX_LINE_LENGTH, file) == NULL) {
        printf("Empty benchmark file\n");
        fclose(file);
        stats->end_time = MPI_Wtime();
        stats->throughput = 0;
        return;
    }
    
    while (fgets(line, MAX_LINE_LENGTH, file)) {
        WeatherData data;
        memset(&data, 0, sizeof(WeatherData));
        
        if (parse_csv_line(line, &data)) {
            ffq_enqueue(queue, data, win);
            stats->items_processed++;
            
            if (stats->items_processed % 100 == 0) {
                printf("Enqueued %d items...\n", stats->items_processed);
            }
            
            if (delay_ms > 0) {
                do_work(delay_ms);
            }
        }
    }
    fclose(file);
    */
    
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