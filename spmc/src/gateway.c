#include "gateway.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>
#include <arpa/inet.h>

// Gateway configuration
static int gateway_port = 5500;
static bool running = false;
static pthread_t listener_thread;
static int server_fd = -1;

// Buffer for incoming data
#define MAX_QUEUE_ITEMS 100
static DataItem data_buffer[MAX_QUEUE_ITEMS];
static int buffer_head = 0;
static int buffer_tail = 0;
static pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;

// For simulation: generate synthetic data 
static bool simulation_mode = true;
static double simulation_rate = 0.1; // seconds between generated items

// Forward declarations
static void* listener_thread_func(void* arg);
static void generate_synthetic_data();

bool gateway_init(int port) {
    gateway_port = port;
    
    if (!simulation_mode) {
        // Create socket
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            perror("Socket creation failed");
            return false;
        }
        
        // Set socket options
        int opt = 1;
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
            perror("setsockopt failed");
            close(server_fd);
            return false;
        }
        
        // Bind to port
        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(gateway_port);
        
        if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
            perror("Bind failed");
            close(server_fd);
            return false;
        }
        
        // Start listening
        if (listen(server_fd, 3) < 0) {
            perror("Listen failed");
            close(server_fd);
            return false;
        }
    }
    
    printf("Gateway initialized, %s on port %d\n", 
           simulation_mode ? "simulation mode" : "listening", 
           gateway_port);
    
    return true;
}

bool gateway_start() {
    running = true;
    
    // Create listener thread
    if (pthread_create(&listener_thread, NULL, listener_thread_func, NULL) != 0) {
        perror("Failed to create listener thread");
        running = false;
        return false;
    }
    
    printf("Gateway started\n");
    return true;
}

bool gateway_get_next(DataItem* item) {
    bool result = false;
    
    pthread_mutex_lock(&buffer_mutex);
    
    if (buffer_head != buffer_tail) {
        // Copy the item
        *item = data_buffer[buffer_head];
        buffer_head = (buffer_head + 1) % MAX_QUEUE_ITEMS;
        result = true;
    }
    
    pthread_mutex_unlock(&buffer_mutex);
    
    return result;
}

void gateway_shutdown() {
    running = false;
    
    if (listener_thread) {
        pthread_join(listener_thread, NULL);
    }
    
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }
    
    printf("Gateway shutdown complete\n");
}

static void* listener_thread_func(void* arg) {
    if (simulation_mode) {
        while (running) {
            generate_synthetic_data();
            usleep(simulation_rate * 1000000); // Convert to microseconds
        }
    } else {
        struct sockaddr_in address;
        int addrlen = sizeof(address);
        
        while (running) {
            // Accept connection
            int new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
            if (new_socket < 0) {
                if (running) {
                    perror("Accept failed");
                }
                continue;
            }
            
            // Read data
            DataItem item;
            ssize_t bytes_read = read(new_socket, &item, sizeof(DataItem));
            close(new_socket);
            
            if (bytes_read == sizeof(DataItem)) {
                // Add to buffer
                pthread_mutex_lock(&buffer_mutex);
                
                // Check if buffer is full
                if ((buffer_tail + 1) % MAX_QUEUE_ITEMS != buffer_head) {
                    data_buffer[buffer_tail] = item;
                    buffer_tail = (buffer_tail + 1) % MAX_QUEUE_ITEMS;
                }
                
                pthread_mutex_unlock(&buffer_mutex);
            }
        }
    }
    
    return NULL;
}

static void generate_synthetic_data() {
    DataItem item;
    
    static int next_id = 1;
    
    // Generate random data
    item.id = next_id++;
    item.value = (double)rand() / RAND_MAX * 100.0;
    item.timestamp = time(NULL);
    snprintf(item.source, sizeof(item.source), "sim-%d", rand() % 10);
    
    // Add to buffer
    pthread_mutex_lock(&buffer_mutex);
    
    // Check if buffer is full
    if ((buffer_tail + 1) % MAX_QUEUE_ITEMS != buffer_head) {
        data_buffer[buffer_tail] = item;
        buffer_tail = (buffer_tail + 1) % MAX_QUEUE_ITEMS;
    }
    
    pthread_mutex_unlock(&buffer_mutex);
} 