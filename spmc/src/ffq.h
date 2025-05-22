#ifndef FFQ_H
#define FFQ_H

#include <stdbool.h>
#include <mpi.h>
#include "data_types.h"

#define EMPTY_CELL -1

/**
 * Cell structure for the queue
 */
typedef struct {
    int rank;       // The rank of the item in the queue (-1 if empty)
    int gap;        // The rank of the first skipped item (-1 if none)
    DataItem data;  // The actual data
} Cell;

/**
 * Fast-Forward Queue
 */
typedef struct {
    int size;             // Size of the queue
    int head;             // Current head position
    int tail;             // Current tail position
    int lastItemDequeued; // Last item dequeued
    Cell cells[];         // Array of cells (allocated dynamically)
} FFQueue;

/**
 * Initialize the FFQ
 * 
 * @param size Size of the queue
 * @param win Pointer to the MPI window
 * @param comm MPI communicator
 * @return Pointer to the FFQ
 */
FFQueue* ffq_init(int size, MPI_Win* win, MPI_Comm comm);

/**
 * Enqueue an item
 * 
 * @param queue Pointer to the queue
 * @param item Item to enqueue
 * @param win MPI window
 * @return True if the item was successfully enqueued
 */
bool ffq_enqueue(FFQueue* queue, DataItem item, MPI_Win win);

/**
 * Dequeue an item
 * 
 * @param queue Pointer to the queue
 * @param consumer_id ID of the consumer
 * @param item Pointer to store the dequeued item
 * @param win MPI window
 * @return True if an item was dequeued
 */
bool ffq_dequeue(FFQueue* queue, int consumer_id, DataItem* item, MPI_Win win);

/**
 * Simulates work by sleeping for a specified duration
 * 
 * @param time_ms Time to sleep in milliseconds
 */
void do_work(int time_ms);

#endif /* FFQ_H */ 