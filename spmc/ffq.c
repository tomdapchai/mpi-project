#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>

#define N 4 
#define ITEMS 10  
#define EMPTY_CELL -1    

typedef struct {
    int data;
    int rank;  
    int gap;       
} Cell;

typedef struct {
    float temperature;
    int timestamp;
} SensorData;

typedef struct {
    Cell cells[N]; 
    int head; 
    int lastItemDequeued; // just for simulation
} SharedData;

void do_work(int time_ms) {
    usleep(time_ms * 1000);
}

void producer(SharedData *shared_data, MPI_Win win) {
    int tail = 0;
    int item = 0;
    
    printf("Producer started\n");
    
    for (int i = 0; i < ITEMS; i++) {
        item = i + 1;
        bool success = false;
        
        // only 1 producer enqueue for all the time
        while (!success) {
            MPI_Win_lock(MPI_LOCK_EXCLUSIVE, 0, 0, win);
            
            int idx = tail % N;
            
            if (shared_data->cells[idx].rank < 0) {
                shared_data->cells[idx].data = item;
                // Memory barrier to ensure data is written before rank
                MPI_Win_flush(0, win);
                shared_data->cells[idx].rank = tail;
                success = true;
                printf("Producer enqueued item %d at cell %d (rank %d)\n", item, idx, tail);
            } else {
                shared_data->cells[idx].gap = tail;
                printf("Producer skipped cell %d (rank %d)\n", idx, tail);
            }
            
            tail++;
            MPI_Win_unlock(0, win);
            
            if (!success) {
                do_work(10);
            }
        }
        do_work(50);
    }
    
    printf("Producer finished\n");
}

void consumer(int consumer_id, SharedData *shared_data, MPI_Win win) {
    printf("Consumer %d started\n", consumer_id);
    
    while (true) {
        // Using MPI atomic operation instead of __sync_fetch_and_add
        MPI_Win_lock(MPI_LOCK_EXCLUSIVE, 0, 0, win);
        int rank = shared_data->head;
        shared_data->head++;
        MPI_Win_unlock(0, win);
        
        MPI_Win_lock(MPI_LOCK_SHARED, 0, 0, win);
        if (shared_data->lastItemDequeued >= ITEMS) {
            MPI_Win_unlock(0, win);
            break;
        }
        MPI_Win_unlock(0, win);
        
        int idx = rank % N;
        bool success = false;
        
        while (!success) {
            MPI_Win_lock(MPI_LOCK_SHARED, 0, 0, win);
            if (shared_data->cells[idx].rank == rank) {
                int data = shared_data->cells[idx].data;
                MPI_Win_unlock(0, win);
                
                MPI_Win_lock(MPI_LOCK_EXCLUSIVE, 0, 0, win);
                shared_data->cells[idx].rank = EMPTY_CELL;
                
                success = true;
                shared_data->lastItemDequeued++;
                MPI_Win_unlock(0, win);
                
                printf("Consumer %d dequeued item %d from cell %d (rank %d)\n", 
                       consumer_id, data, idx, rank);
                
                do_work(200);
            } 
            else if (shared_data->cells[idx].gap >= rank) {
                MPI_Win_unlock(0, win);
                
                MPI_Win_lock(MPI_LOCK_EXCLUSIVE, 0, 0, win);
                rank = shared_data->head;
                shared_data->head++;
                MPI_Win_unlock(0, win);
                
                idx = rank % N;
                printf("Consumer %d skipped to rank %d (cell %d)\n", 
                       consumer_id, rank, idx);
            } 
            else {
                MPI_Win_unlock(0, win);
                do_work(300); // wait for producer to enqueue new items
            }
        }
    }
    
    printf("Consumer %d finished\n", consumer_id);
}

int main(int argc, char **argv) {
    int rank, size;
    MPI_Win win;
    SharedData *shared_data;
    MPI_Aint size_bytes;
    int disp_unit;
    
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    
    // allocate shared memory
    size_bytes = sizeof(SharedData);
    MPI_Win_allocate_shared(size_bytes, sizeof(char), MPI_INFO_NULL, 
                            MPI_COMM_WORLD, &shared_data, &win);
    
    // get pointer to shared memory from rank 0 for consumer
    if (rank != 0) {
        MPI_Win_shared_query(win, 0, &size_bytes, &disp_unit, &shared_data);
    }
    
    // initialize shared memory, producer takes care of it
    if (rank == 0) {
        for (int i = 0; i < N; i++) {
            shared_data->cells[i].rank = EMPTY_CELL;
            shared_data->cells[i].gap = EMPTY_CELL;
            shared_data->cells[i].data = 0;
        }
        shared_data->head = 0;
        shared_data->lastItemDequeued = 0;
    }
    
    // make sure all processes see the initialized data
    MPI_Barrier(MPI_COMM_WORLD);
    
    if (rank == 0) {
        producer(shared_data, win);
    } else {
        consumer(rank, shared_data, win);
    }
    
    MPI_Barrier(MPI_COMM_WORLD);
    
    MPI_Win_free(&win);
    MPI_Finalize();
    
    return 0;
}