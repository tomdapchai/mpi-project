#include "ffq_optimized.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stddef.h>
#include <string.h>

void do_work(int time_ms) {
    usleep(time_ms * 1000);
}

// Create MPI datatype for WeatherData - now returns it for caching
static MPI_Datatype create_weather_data_type() {
    MPI_Datatype weather_type;
    int blocklengths[] = {MAX_TIMESTAMP_LEN, MAX_CITY_LEN, 1, MAX_ICON_LEN, 1, 1, 1};
    MPI_Datatype types[] = {MPI_CHAR, MPI_CHAR, MPI_INT, MPI_CHAR, MPI_FLOAT, MPI_INT, MPI_C_BOOL};
    MPI_Aint offsets[7];
    
    offsets[0] = offsetof(WeatherData, timestamp);
    offsets[1] = offsetof(WeatherData, city);
    offsets[2] = offsetof(WeatherData, aqi);
    offsets[3] = offsetof(WeatherData, weather_icon);
    offsets[4] = offsetof(WeatherData, wind_speed);
    offsets[5] = offsetof(WeatherData, humidity);
    offsets[6] = offsetof(WeatherData, valid);
    
    MPI_Type_create_struct(7, blocklengths, offsets, types, &weather_type);
    MPI_Type_commit(&weather_type);
    
    return weather_type;
}

FFQHandle* ffq_init_optimized(int size, MPI_Win* win, MPI_Comm comm) {
    int rank;
    MPI_Comm_rank(comm, &rank);

    FFQueue* queue = NULL;
    
    // Calculate size needed for the window
    MPI_Aint win_size = sizeof(FFQueue) + size * sizeof(Cell);
    
    // Allocate the window
    if (rank == 0) {
        MPI_Win_allocate(win_size, 1, MPI_INFO_NULL, comm, &queue, win);
        
        // Initialize queue
        queue->size = size;
        queue->head = 0;
        queue->tail = 0;
        queue->lastItemDequeued = 0;
        
        // Initialize cells
        for (int i = 0; i < size; i++) {
            queue->cells[i].rank = EMPTY_CELL;
            queue->cells[i].gap = EMPTY_CELL;
            memset(&(queue->cells[i].data), 0, sizeof(WeatherData));
            queue->cells[i].data.valid = false;
        }
    } else {
        // Only rank 0 allocates memory, others just create the window
        MPI_Win_allocate(0, 1, MPI_INFO_NULL, comm, &queue, win);
    }
    
    // Ensure all processes see initialized data
    MPI_Barrier(comm);
    
    // Create handle structure with cached data
    FFQHandle* handle = (FFQHandle*)malloc(sizeof(FFQHandle));
    handle->queue = queue;
    handle->win = *win;
    handle->local_rank = rank;
    
    // Cache the queue size locally (it never changes)
    if (rank == 0) {
        handle->local_size = size;
    } else {
        // Non-root processes need to read it once
        MPI_Win_lock(MPI_LOCK_SHARED, 0, 0, *win);
        MPI_Get(&handle->local_size, 1, MPI_INT, 0, 
                offsetof(FFQueue, size), 1, MPI_INT, *win);
        MPI_Win_flush(0, *win);
        MPI_Win_unlock(0, *win);
    }
    
    // Create and cache the weather datatype (MAJOR OPTIMIZATION)
    handle->weather_type = create_weather_data_type();
    
    return handle;
}

void ffq_cleanup_optimized(FFQHandle* handle) {
    if (handle) {
        if (handle->weather_type != MPI_DATATYPE_NULL) {
            MPI_Type_free(&handle->weather_type);
        }
        free(handle);
    }
}

bool ffq_enqueue_optimized(FFQHandle* handle, WeatherData item) {
    bool success = false;
    int local_tail = handle->queue->tail; // Cache the tail value
    int backoff_us = 100;  // Start with 100 microseconds
    const int MAX_BACKOFF = 10000;  // Max 10ms
    
    while (!success) {
        // OPTIMIZATION: Single lock for the entire operation
        MPI_Win_lock(MPI_LOCK_EXCLUSIVE, 0, 0, handle->win);
        
        int idx = local_tail % handle->local_size;
        
        // Read the cell's rank value
        int cell_rank;
        MPI_Get(&cell_rank, 1, MPI_INT, 0, 
                offsetof(FFQueue, cells[idx].rank), 
                1, MPI_INT, handle->win);
        MPI_Win_flush(0, handle->win);
        
        if (cell_rank < 0) {
            // Cell is free - OPTIMIZATION: Batch Put operations before flush
            // Write data first
            MPI_Put(&item, 1, handle->weather_type, 0, 
                    offsetof(FFQueue, cells[idx].data), 
                    1, handle->weather_type, handle->win);
            
            // Then update the rank to mark as used
            MPI_Put(&local_tail, 1, MPI_INT, 0, 
                    offsetof(FFQueue, cells[idx].rank), 
                    1, MPI_INT, handle->win);
            
            // Update tail
            local_tail++;
            MPI_Put(&local_tail, 1, MPI_INT, 0, 
                    offsetof(FFQueue, tail), 
                    1, MPI_INT, handle->win);
            
            // OPTIMIZATION: Single flush for all operations
            MPI_Win_flush(0, handle->win);
            
            success = true;
            printf("Producer enqueued item for city %s at cell %d (rank %d)\n", 
                   item.city, idx, local_tail - 1);
        } else {
            // Cell is in use - mark as gap and update tail
            MPI_Put(&local_tail, 1, MPI_INT, 0, 
                    offsetof(FFQueue, cells[idx].gap), 
                    1, MPI_INT, handle->win);
            
            local_tail++;
            MPI_Put(&local_tail, 1, MPI_INT, 0, 
                    offsetof(FFQueue, tail), 
                    1, MPI_INT, handle->win);
            
            // OPTIMIZATION: Batch flush
            MPI_Win_flush(0, handle->win);
            
            printf("Producer skipped cell %d (rank %d)\n", idx, local_tail - 1);
        }
        
        MPI_Win_unlock(0, handle->win);
        
        // OPTIMIZATION: Adaptive backoff
        if (!success) {
            usleep(backoff_us);
            backoff_us = (backoff_us * 2 > MAX_BACKOFF) ? MAX_BACKOFF : backoff_us * 2;
        }
    }
    
    return success;
}

bool ffq_dequeue_optimized(FFQHandle* handle, int consumer_id, WeatherData* item) {
    int fetch_rank = 0;
    int backoff_us = 100;  // Adaptive backoff
    const int MAX_BACKOFF = 10000;
    
    // OPTIMIZATION: Use compare-and-swap pattern for atomic fetch-and-increment
    // This is more efficient than Get_accumulate in many implementations
    MPI_Win_lock(MPI_LOCK_EXCLUSIVE, 0, 0, handle->win);
    MPI_Get_accumulate(&(int){1}, 1, MPI_INT, 
                      &fetch_rank, 1, MPI_INT, 
                      0, offsetof(FFQueue, head), 1, 
                      MPI_INT, MPI_SUM, handle->win);
    MPI_Win_flush(0, handle->win);
    MPI_Win_unlock(0, handle->win);
    
    int idx = fetch_rank % handle->local_size;
    bool success = false;
    int retry_count = 0;
    const int MAX_RETRIES = 1000;  // Prevent infinite loops
    
    while (!success && retry_count < MAX_RETRIES) {
        retry_count++;
        
        // OPTIMIZATION: Use shared lock for reading, reduces contention
        MPI_Win_lock(MPI_LOCK_SHARED, 0, 0, handle->win);
        
        // OPTIMIZATION: Batch all Get operations together
        int cell_rank, cell_gap, lastItem;
        WeatherData cell_data;
        
        MPI_Get(&cell_rank, 1, MPI_INT, 0, 
                offsetof(FFQueue, cells[idx].rank), 
                1, MPI_INT, handle->win);
        MPI_Get(&cell_gap, 1, MPI_INT, 0, 
                offsetof(FFQueue, cells[idx].gap), 
                1, MPI_INT, handle->win);
        MPI_Get(&cell_data, 1, handle->weather_type, 0, 
                offsetof(FFQueue, cells[idx].data), 
                1, handle->weather_type, handle->win);
        MPI_Get(&lastItem, 1, MPI_INT, 0, 
                offsetof(FFQueue, lastItemDequeued), 
                1, MPI_INT, handle->win);
        
        // OPTIMIZATION: Single flush for all Get operations
        MPI_Win_flush(0, handle->win);
        MPI_Win_unlock(0, handle->win);
        
        if (cell_rank == fetch_rank) {
            // Item found, dequeue it
            *item = cell_data;
            
            // OPTIMIZATION: Exclusive lock only for write operations
            MPI_Win_lock(MPI_LOCK_EXCLUSIVE, 0, 0, handle->win);
            
            // Batch Put operations
            int empty = EMPTY_CELL;
            int new_last = lastItem + 1;
            
            MPI_Put(&empty, 1, MPI_INT, 0, 
                    offsetof(FFQueue, cells[idx].rank), 
                    1, MPI_INT, handle->win);
            MPI_Put(&new_last, 1, MPI_INT, 0, 
                    offsetof(FFQueue, lastItemDequeued), 
                    1, MPI_INT, handle->win);
            
            // OPTIMIZATION: Single flush for both operations
            MPI_Win_flush(0, handle->win);
            MPI_Win_unlock(0, handle->win);
            
            success = true;
            printf("Consumer %d dequeued item for (timestamp %s, city %s, aqi %d, wind_speed %f, humidity %d) from cell %d (rank %d)\n", 
                   consumer_id, item->timestamp, item->city, item->aqi, item->wind_speed, item->humidity, idx, fetch_rank);
        } 
        else if (cell_gap >= fetch_rank && cell_rank != fetch_rank) {
            // Cell was skipped, move to next rank
            MPI_Win_lock(MPI_LOCK_EXCLUSIVE, 0, 0, handle->win);
            MPI_Get_accumulate(&(int){1}, 1, MPI_INT, 
                              &fetch_rank, 1, MPI_INT, 
                              0, offsetof(FFQueue, head), 1, 
                              MPI_INT, MPI_SUM, handle->win);
            MPI_Win_flush(0, handle->win);
            MPI_Win_unlock(0, handle->win);
            
            idx = fetch_rank % handle->local_size;
            printf("Consumer %d skipped to rank %d (cell %d)\n", 
                   consumer_id, fetch_rank, idx);
            
            // Reset backoff on progress
            backoff_us = 100;
        } 
        else {
            // Wait for producer to write data - OPTIMIZATION: Adaptive backoff
            usleep(backoff_us);
            backoff_us = (backoff_us * 2 > MAX_BACKOFF) ? MAX_BACKOFF : backoff_us * 2;
        }
    }
    
    if (!success && retry_count >= MAX_RETRIES) {
        fprintf(stderr, "Consumer %d: Dequeue timeout after %d retries\n", 
                consumer_id, retry_count);
    }
    
    return success;
}
