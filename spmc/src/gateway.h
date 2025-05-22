/**
 * Gateway for receiving external data streams
 */
#ifndef GATEWAY_H
#define GATEWAY_H

#include <stdbool.h>
#include "data_types.h"

/**
 * Initialize the gateway
 * 
 * @param port The port number to listen on
 * @return True if initialization was successful, false otherwise
 */
bool gateway_init(int port);

/**
 * Start the gateway listener in a background thread
 * 
 * @return True if started successfully, false otherwise
 */
bool gateway_start();

/**
 * Get the next data item from the gateway
 * 
 * @param item Pointer to where the item should be stored
 * @return True if an item was retrieved, false if no item is available
 */
bool gateway_get_next(DataItem* item);

/**
 * Shutdown the gateway
 */
void gateway_shutdown();

#endif /* GATEWAY_H */ 