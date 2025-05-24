#ifndef TEST_MODE_H
#define TEST_MODE_H

#include <stdbool.h>
#include <mpi.h>
#include "ffq.h"
#include "weather_data.h"

// Generate test data for TEST_MODE
WeatherData generate_test_data(int item_number);

// Run producer in test mode
void run_producer(FFQueue *queue, int num_items, int delay_ms, MPI_Win win);

// Run consumer in test mode
void run_consumer(FFQueue *queue, int consumer_id, int num_items, int delay_ms, MPI_Win win);

#endif // TEST_MODE_H