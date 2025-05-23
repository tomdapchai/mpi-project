#ifndef FFQ_H
#define FFQ_H

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

// Initialization function
FFQueue *ffq_init(int size, MPI_Win *win, MPI_Comm comm);

// Enqueue function (for producer)
bool ffq_enqueue(FFQueue *queue, WeatherData item, MPI_Win win);

// Dequeue function (for consumers)
bool ffq_dequeue(FFQueue *queue, int consumer_id, WeatherData *item, MPI_Win win);

// Simulated work function
void do_work(int time_ms);

#endif