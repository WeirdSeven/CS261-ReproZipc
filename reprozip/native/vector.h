#ifndef VECTOR_H
#define VECTOR_H

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/ptrace.h>
#include <sys/reg.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>
#include <poll.h>

#define VECTOR_INIT_CAPACITY 4

typedef struct {
    pid_t tid;
    int worker_pipe[4];
} tid_worker_pipe;

typedef struct vector {
    tid_worker_pipe *items;
    int capacity;
    int total;
} vector;

typedef struct {
	struct pollfd *items;
	int capacity;
	int total;
} vpollfd;

void vector_init(vector *);
int vector_total(vector *);
void vector_resize(vector *, int);
void vector_add(vector *, tid_worker_pipe);
void vector_set(vector *, int, tid_worker_pipe);
tid_worker_pipe vector_get(vector *, int);
void vector_delete(vector *, int);
//int vector_find(vector *, tid_worker_pipe);
void vector_free(vector *);
int vector_find_tid(vector *, pid_t);

void vpollfd_init(vpollfd *);
int vpollfd_total(vpollfd *);
void vpollfd_resize(vpollfd *, int);
void vpollfd_add(vpollfd *, struct pollfd);
void vpollfd_set(vpollfd *, int, struct pollfd);
struct pollfd vpollfd_get(vpollfd *, int);
void vpollfd_delete(vpollfd *, int);
//int vpollfd_find(vpollfd *, struct pollfd);
void vpollfd_free(vpollfd *);
struct pollfd *vpollfd_items(vpollfd *);

#endif