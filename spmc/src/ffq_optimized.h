#ifndef FFQ_OPTIMIZED_H
#define FFQ_OPTIMIZED_H

#include <stdbool.h>
#include <mpi.h>
#include "weather_data.h"

#define EMPTY_CELL -1

typedef struct
{
    int rank;
    int gap;
    WeatherData data;
} Cell;

typedef struct
{
    int size;
    int head;
    int tail;
    int lastItemDequeued;
    Cell cells[];
} FFQueue;

// Handle structure for optimized operations
typedef struct
{
    FFQueue *queue;
    MPI_Win win;
    int local_size;            // Cached queue size (never changes)
    int local_rank;            // Process rank
    MPI_Datatype weather_type; // Cached datatype
} FFQHandle;

// Initialization functions
FFQHandle *ffq_init_optimized(int size, MPI_Win *win, MPI_Comm comm);
void ffq_cleanup_optimized(FFQHandle *handle);

// Optimized enqueue function (for producer)
bool ffq_enqueue_optimized(FFQHandle *handle, WeatherData item);

// Optimized dequeue function (for consumers)
bool ffq_dequeue_optimized(FFQHandle *handle, int consumer_id, WeatherData *item);

// Simulated work function
void do_work(int time_ms);

#endif
