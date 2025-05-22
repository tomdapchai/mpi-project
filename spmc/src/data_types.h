/**
 * Data types for the FFQ implementation
 */
#ifndef DATA_TYPES_H
#define DATA_TYPES_H

#include <time.h>

/**
 * DataItem represents a single item in the queue
 * This simulates data that might come from an external stream
 */
typedef struct {
    int id;                 // Unique identifier
    double value;           // Sample data value
    time_t timestamp;       // Timestamp when data was created
    char source[32];        // Source identifier
} DataItem;

#endif /* DATA_TYPES_H */ 