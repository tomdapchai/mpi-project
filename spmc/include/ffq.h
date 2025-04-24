#ifndef FFQ_H
#define FFQ_H

#include <mpi.h>
#include <stdbool.h>

#define EMPTY_CELL -1

typedef struct {
    int data;      // Actual data to store
    int rank;      // Rank of item (or -1 if cell unused)
    int gap;       // Gap in numbering (skipped item)
} Cell;

typedef struct {
    int size;      // Size of the queue
    int head;      // Head counter (monotonically increasing)
    int tail;      // Tail counter (monotonically increasing) - local to producer
    int lastItemDequeued; // For simulation tracking
    Cell cells[]; // Flexible array member - cells will be allocated after this struct
} FFQueue;

// Initialize the queue
FFQueue* ffq_init(int size, MPI_Win* win, MPI_Comm comm);

// Enqueue an item (producer only)
bool ffq_enqueue(FFQueue* queue, int item, MPI_Win win);

// Dequeue an item (consumers)
bool ffq_dequeue(FFQueue* queue, int consumer_id, int* item, MPI_Win win);

// Wait between operations
void do_work(int time_ms);

#endif // FFQ_H