#include "ffq.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void do_work(int time_ms) {
    usleep(time_ms * 1000);
}

FFQueue* ffq_init(int size, MPI_Win* win, MPI_Comm comm) {
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
            queue->cells[i].data = 0;
        }
    } else {
        // Only rank 0 allocates memory, others just create the window
        MPI_Win_allocate(0, 1, MPI_INFO_NULL, comm, &queue, win);
    }
    
    // Ensure all processes see initialized data
    MPI_Barrier(comm);
    
    return queue;
}

bool ffq_enqueue(FFQueue* queue, int item, MPI_Win win) {
    bool success = false;
    int local_tail = queue->tail; // Cache the tail value
    
    while (!success) {
        MPI_Win_lock(MPI_LOCK_EXCLUSIVE, 0, 0, win);
        
        int idx = local_tail % queue->size;
        
        // Read the cell's rank value
        int cell_rank;
        MPI_Get(&cell_rank, 1, MPI_INT, 0, 
                offsetof(FFQueue, cells[idx].rank), 
                1, MPI_INT, win);
        MPI_Win_flush(0, win);
        
        if (cell_rank < 0) {
            // Cell is free, write data first
            MPI_Put(&item, 1, MPI_INT, 0, 
                    offsetof(FFQueue, cells[idx].data), 
                    1, MPI_INT, win);
            MPI_Win_flush(0, win);
            
            // Then update the rank to mark as used
            MPI_Put(&local_tail, 1, MPI_INT, 0, 
                    offsetof(FFQueue, cells[idx].rank), 
                    1, MPI_INT, win);
            MPI_Win_flush(0, win);
            
            success = true;
            printf("Producer enqueued item %d at cell %d (rank %d)\n", 
                   item, idx, local_tail);
        } else {
            // Cell is in use, mark as gap
            MPI_Put(&local_tail, 1, MPI_INT, 0, 
                    offsetof(FFQueue, cells[idx].gap), 
                    1, MPI_INT, win);
            MPI_Win_flush(0, win);
            
            printf("Producer skipped cell %d (rank %d)\n", idx, local_tail);
        }
        
        // Update tail
        local_tail++;
        MPI_Put(&local_tail, 1, MPI_INT, 0, 
                offsetof(FFQueue, tail), 
                1, MPI_INT, win);
        MPI_Win_flush(0, win);
        
        MPI_Win_unlock(0, win);
        
        if (!success) {
            do_work(10); // Small backoff
        }
    }
    
    return success;
}

bool ffq_dequeue(FFQueue* queue, int consumer_id, int* item, MPI_Win win) {
    int fetch_rank = 0;
    
    // Atomically fetch and increment the head
    MPI_Win_lock(MPI_LOCK_EXCLUSIVE, 0, 0, win);
    
    // Get current head value
    MPI_Get_accumulate(&(int){1}, 1, MPI_INT, 
                      &fetch_rank, 1, MPI_INT, 
                      0, offsetof(FFQueue, head), 1, 
                      MPI_INT, MPI_SUM, win);
    MPI_Win_flush(0, win);
    MPI_Win_unlock(0, win);
    
    int local_size = 0;
    MPI_Win_lock(MPI_LOCK_SHARED, 0, 0, win);
    MPI_Get(&local_size, 1, MPI_INT, 0, offsetof(FFQueue, size), 1, MPI_INT, win);
    MPI_Win_flush(0, win);
    MPI_Win_unlock(0, win);
    
    int idx = fetch_rank % local_size;
    bool success = false;
    
    while (!success) {
        MPI_Win_lock(MPI_LOCK_SHARED, 0, 0, win);
        
        // Read cell values
        int cell_rank, cell_gap, cell_data;
        MPI_Get(&cell_rank, 1, MPI_INT, 0, 
                offsetof(FFQueue, cells[idx].rank), 
                1, MPI_INT, win);
        MPI_Get(&cell_gap, 1, MPI_INT, 0, 
                offsetof(FFQueue, cells[idx].gap), 
                1, MPI_INT, win);
        MPI_Get(&cell_data, 1, MPI_INT, 0, 
                offsetof(FFQueue, cells[idx].data), 
                1, MPI_INT, win);
        MPI_Win_flush(0, win);
        
        // Check if item has been dequeued already
        int lastItem;
        MPI_Get(&lastItem, 1, MPI_INT, 0, 
                offsetof(FFQueue, lastItemDequeued), 
                1, MPI_INT, win);
        MPI_Win_flush(0, win);
        MPI_Win_unlock(0, win);
        
        if (cell_rank == fetch_rank) {
            // Item found, dequeue it
            *item = cell_data;
            
            MPI_Win_lock(MPI_LOCK_EXCLUSIVE, 0, 0, win);
            
            // Mark cell as empty
            int empty = EMPTY_CELL;
            MPI_Put(&empty, 1, MPI_INT, 0, 
                    offsetof(FFQueue, cells[idx].rank), 
                    1, MPI_INT, win);
            MPI_Win_flush(0, win);
            
            // Update dequeue counter
            int new_last = lastItem + 1;
            MPI_Put(&new_last, 1, MPI_INT, 0, 
                    offsetof(FFQueue, lastItemDequeued), 
                    1, MPI_INT, win);
            MPI_Win_flush(0, win);
            
            MPI_Win_unlock(0, win);
            
            success = true;
            printf("Consumer %d dequeued item %d from cell %d (rank %d)\n", 
                   consumer_id, *item, idx, fetch_rank);
        } 
        else if (cell_gap >= fetch_rank && cell_rank != fetch_rank) {
            // Cell was skipped, move to next rank
            MPI_Win_lock(MPI_LOCK_EXCLUSIVE, 0, 0, win);
            
            // Atomically get the next rank
            MPI_Get_accumulate(&(int){1}, 1, MPI_INT, 
                              &fetch_rank, 1, MPI_INT, 
                              0, offsetof(FFQueue, head), 1, 
                              MPI_INT, MPI_SUM, win);
            MPI_Win_flush(0, win);
            MPI_Win_unlock(0, win);
            
            idx = fetch_rank % local_size;
            printf("Consumer %d skipped to rank %d (cell %d)\n", 
                   consumer_id, fetch_rank, idx);
        } 
        else {
            // Wait for producer to write data
            do_work(10);
        }
    }
    
    return success;
}