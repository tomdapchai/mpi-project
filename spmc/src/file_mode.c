#include "file_mode.h"
#include "common.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

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