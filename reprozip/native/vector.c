#include "vector.h"

void vector_init(vector *v)
{
    //printf("P1\n");
    v->capacity = VECTOR_INIT_CAPACITY;
    //printf("P2\n");
    v->total = 0;
    //printf("P3\n");
    v->items = malloc(sizeof(tid_worker_pipe) * v->capacity);
    //printf("P4\n");
}

int vector_total(vector *v)
{
    return v->total;
}

/*int vector_find(vector *v, tid_worker_pipe item) {
    for (int i = 0; i < v->total; i++) {
        if (v->items[i] == item)
            return i;
    }
    return -1;
}*/

int vector_find_tid(vector *v, pid_t tid) {
    //printf("Beginning of find tid\n");
    //printf("Total is: [%d]\n", v->total);
    for (int i = 0; i < v->total; i++) {
        if (v->items[i].tid == tid)
            return i;
    }
    return -1;
}

void vector_resize(vector *v, int capacity)
{
    #ifdef DEBUG_ON
    printf("vector_resize: %d to %d\n", v->capacity, capacity);
    #endif

    tid_worker_pipe *items = realloc(v->items, sizeof(tid_worker_pipe) * capacity);
    if (items) {
        v->items = items;
        v->capacity = capacity;
    }
}

void vector_add(vector *v, tid_worker_pipe item)
{
    if (v->capacity == v->total)
        vector_resize(v, v->capacity * 2);
    v->items[v->total++] = item;
}

void vector_set(vector *v, int index, tid_worker_pipe item)
{
    if (index >= 0 && index < v->total)
        v->items[index] = item;
}

tid_worker_pipe vector_get(vector *v, int index)
{
    //if (index >= 0 && index < v->total)
        return v->items[index];
    //return -1;
}

void vector_delete(vector *v, int index)
{
    if (index < 0 || index >= v->total)
        return;

    //v->items[index] = NULL;

    int i;
    for (i = index; i < v->total - 1; i++) {
        v->items[i] = v->items[i + 1];
        //v->items[i + 1] = NULL;
    }

    v->total--;

    if (v->total > 0 && v->total == v->capacity / 4)
        vector_resize(v, v->capacity / 2);
}

void vector_free(vector *v)
{
    free(v->items);
}


/********  vpollfd vector    ****************/


void vpollfd_init(vpollfd *v)
{
    v->capacity = VECTOR_INIT_CAPACITY;
    v->total = 0;
    v->items = malloc(sizeof(struct pollfd) * v->capacity);
}

int vpollfd_total(vpollfd *v)
{
    return v->total;
}

struct pollfd *vpollfd_items(vpollfd *v) 
{
    return v->items;
}

/*int vpollfd_find(vpollfd *v, struct pollfd item) {
    for (int i = 0; i < v->total; i++) {
        if (v->items[i] == item)
            return i;
    }
    return -1;
}*/

void vpollfd_resize(vpollfd *v, int capacity)
{
    #ifdef DEBUG_ON
    printf("vector_resize: %d to %d\n", v->capacity, capacity);
    #endif

    struct pollfd *items = realloc(v->items, sizeof(struct pollfd) * capacity);
    if (items) {
        v->items = items;
        v->capacity = capacity;
    }
}

void vpollfd_add(vpollfd *v, struct pollfd item)
{
    if (v->capacity == v->total)
        vpollfd_resize(v, v->capacity * 2);
    v->items[v->total++] = item;
}

void vpollfd_set(vpollfd *v, int index, struct pollfd item)
{
    if (index >= 0 && index < v->total)
        v->items[index] = item;
}

struct pollfd vpollfd_get(vpollfd *v, int index)
{
    //if (index >= 0 && index < v->total)
        return v->items[index];
    //return -1;
}

void vpollfd_delete(vpollfd *v, int index)
{
    if (index < 0 || index >= v->total)
        return;

    //v->items[index] = NULL;

    int i;
    for (i = index; i < v->total - 1; i++) {
        v->items[i] = v->items[i + 1];
        //v->items[i + 1] = NULL;
    }

    v->total--;

    if (v->total > 0 && v->total == v->capacity / 4)
        vpollfd_resize(v, v->capacity / 2);
}

void vpollfd_free(vpollfd *v)
{
    free(v->items);
}