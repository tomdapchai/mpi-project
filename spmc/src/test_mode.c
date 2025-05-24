#include "test_mode.h"
#include <stdio.h>
#include <stddef.h>

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