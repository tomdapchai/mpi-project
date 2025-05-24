#ifndef FILE_MODE_H
#define FILE_MODE_H

#include <stdbool.h>
#include <mpi.h>
#include "ffq.h"
#include "weather_data.h"

// Parse a CSV line into a WeatherData struct
bool parse_csv_line(char *line, WeatherData *data);

// Run producer in file mode - continuously reads from a CSV file
void run_file_producer(FFQueue *queue, const char *csv_file, int delay_ms, MPI_Win win);

// Run consumer in file mode
void run_file_consumer(FFQueue *queue, int consumer_id, int delay_ms, MPI_Win win);

#endif // FILE_MODE_H