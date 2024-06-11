#ifndef UTILITIES_MY_UTILS_H
#define UTILITIES_MY_UTILS_H
#define _GNU_SOURCE

#ifdef DEBUG
#define debug_print(fmt, ...) printf("DEBUG: " fmt, ##__VA_ARGS__)
#else
#define debug_print(fmt, ...) do {} while (0)
#endif

// Standard Linux Dependencies:
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
// Additional Linux Dependencies:
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <features.h>
#include <math.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>

typedef struct q_node {
    void* data;
    struct q_node* next;
} q_node_t;

typedef struct {
    q_node_t* head, * tail;
} queue_t;

void* open_file_and_map_to_shared_memory(const char* path);

void* my_malloc(size_t size);

void print_hex(const char* data, size_t size);

/**
 * @brief  Merges two character arrays, and returns the concatenation of the
 * arrays (unsorted).
 */
void** merge_arrays(void** a, void** b, int asize, int bsize);

/**
 * @brief  Truncates a string if it is longer than a specified length, else does
 * nothing.
 * @param  str: The string to truncate
 * @param  limit: The max length of the string
 */
char* truncate_string(char* str, int limit);

int check_null(void* obj);

int check_err(int return_value, char* error_msg);

/* The following code pertains to my queue in linked list ADT, inspired by my
 * code from "Assignment 2: Multi Type Linked List" */

 // Initialies an empty queue on the heap, returning a pointer.
queue_t* q_init();

// Enqueues a queue element, storing data in the end of the linked list and
// allocating memory.
void q_enqueue(queue_t* qobj, void* data);

// Dequeues a node, removing it from the head of the list and deallocating
// memory.
void* q_dequeue(queue_t* qobj);

int q_empty(queue_t* qobj);

// Destroys the queue, freeing all dynamically allocated memory.
void q_destroy(queue_t* qobj);

#endif