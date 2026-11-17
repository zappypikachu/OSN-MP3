#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include "bag.h"

typedef enum {
    HANDLER_IDLE,
    HANDLER_WAITING_FOR_BELT,
    HANDLER_ASSIGNED,
    HANDLER_LOADING,
    HANDLER_DONE
} HandlerState;

/* Bags currently on the belt */
static Bag belt[BELT_CAPACITY];
static int belt_count;

/* Bags waiting to be assigned to handlers */
static Bag ready[MAX_BAGS];
static int ready_front, ready_rear, ready_count;

/* Handler information */
static HandlerState handler_state[HANDLER_COUNT];
static Bag handler_bag[HANDLER_COUNT];
static pthread_t handlers[HANDLER_COUNT];
static int handler_created[HANDLER_COUNT];

/* Slots reserved by handlers currently loading */
static int reserved_slots;
static int handler_shutdown;

static pthread_mutex_t baggage_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t baggage_cond = PTHREAD_COND_INITIALIZER;


/* Check whether a belt slot is available, including reservations. */
static int belt_has_space_locked(void)
{
    return belt_count + reserved_slots < BELT_CAPACITY;
}


/* Find a handler in a particular state. */
static int find_handler_locked(HandlerState wanted)
{
    for (int h = 0; h < HANDLER_COUNT; h++) {
        if (handler_created[h] && handler_state[h] == wanted)
            return h;
    }

    return -1;
}


/* Remove the oldest ready bag. */
static Bag pop_ready_locked(void)
{
    Bag bag = ready[ready_front];

    ready_front = (ready_front + 1) % MAX_BAGS;
    ready_count--;

    return bag;
}


/* Start loading and wait for the handler to acknowledge. */
static int start_loading_locked(int h, int *handler_id, Bag *bag)
{
    handler_state[h] = HANDLER_ASSIGNED;
    reserved_slots++;

    pthread_cond_broadcast(&baggage_cond);

    while (handler_state[h] == HANDLER_ASSIGNED && !handler_shutdown)
        pthread_cond_wait(&baggage_cond, &baggage_mutex);

    if (handler_state[h] != HANDLER_LOADING) {
        reserved_slots--;
        return 0;
    }

    *handler_id = h;
    *bag = handler_bag[h];

    return 1;
}


/* Initialize baggage data structures. */
void baggage_init(void)
{
    pthread_mutex_lock(&baggage_mutex);

    belt_count = 0;

    ready_front = 0;
    ready_rear = 0;
    ready_count = 0;

    reserved_slots = 0;
    handler_shutdown = 0;

    for (int h = 0; h < HANDLER_COUNT; h++) {
        handler_state[h] = HANDLER_IDLE;
        handler_created[h] = 0;
    }

    pthread_mutex_unlock(&baggage_mutex);
}


/* Submit a newly stamped student's bag to the ready queue. */
int baggage_submit(int student_id)
{
    pthread_mutex_lock(&baggage_mutex);

    if (ready_count == MAX_BAGS) {
        pthread_mutex_unlock(&baggage_mutex);
        return -1;
    }

    ready[ready_rear].student_id = student_id;

    ready_rear = (ready_rear + 1) % MAX_BAGS;
    ready_count++;

    pthread_mutex_unlock(&baggage_mutex);

    return 0;
}


/*
 * Assign a ready bag to a handler.
 *
 * If the belt is full, the handler retains the bag.
 * Returns 1 when a loading event can start.
 * Returns 0 when no loading event can currently start.
 */
int baggage_assign_next(int *handler_id, Bag *bag)
{
    if (handler_id == NULL || bag == NULL)
        return 0;

    pthread_mutex_lock(&baggage_mutex);

    if (handler_shutdown) {
        pthread_mutex_unlock(&baggage_mutex);
        return 0;
    }

    /* Give priority to handlers already holding bags. */
    if (belt_has_space_locked()) {

        int h = find_handler_locked(HANDLER_WAITING_FOR_BELT);

        if (h != -1) {
            int result = start_loading_locked(h, handler_id, bag);

            pthread_mutex_unlock(&baggage_mutex);
            return result;
        }
    }

    /* Assign ready bags to idle handlers. */
    while (ready_count > 0) {

        int h = find_handler_locked(HANDLER_IDLE);

        if (h == -1)
            break;

        handler_bag[h] = pop_ready_locked();

        if (belt_has_space_locked()) {

            int result = start_loading_locked(h, handler_id, bag);

            pthread_mutex_unlock(&baggage_mutex);
            return result;
        }

        /* Handler keeps the bag until belt space becomes available. */
        handler_state[h] = HANDLER_WAITING_FOR_BELT;
    }

    pthread_mutex_unlock(&baggage_mutex);

    return 0;
}


/* Complete loading and put the handler's bag onto the belt. */
int baggage_load_complete(int handler_id, int *student_id)
{
    if (handler_id < 0 || handler_id >= HANDLER_COUNT)
        return -1;

    pthread_mutex_lock(&baggage_mutex);

    if (handler_state[handler_id] != HANDLER_LOADING ||
        reserved_slots == 0 ||
        belt_count == BELT_CAPACITY) {

        pthread_mutex_unlock(&baggage_mutex);
        return -1;
    }

    Bag bag = handler_bag[handler_id];

    belt[belt_count++] = bag;
    reserved_slots--;

    if (student_id != NULL)
        *student_id = bag.student_id;

    /* Ask the handler thread to acknowledge completion. */
    handler_state[handler_id] = HANDLER_DONE;

    pthread_cond_broadcast(&baggage_cond);

    while (handler_state[handler_id] == HANDLER_DONE && !handler_shutdown)
        pthread_cond_wait(&baggage_cond, &baggage_mutex);

    int result = handler_state[handler_id] == HANDLER_IDLE ? 0 : -1;

    pthread_mutex_unlock(&baggage_mutex);

    return result;
}

/* Check whether a particular student's bag is on the belt. */
int baggage_has_bag(int student_id)
{
    int found = 0;

    pthread_mutex_lock(&baggage_mutex);

    for (int i = 0; i < belt_count; i++) {

        if (belt[i].student_id == student_id) {
            found = 1;
            break;
        }
    }

    pthread_mutex_unlock(&baggage_mutex);

    return found;
}


/* Remove a particular student's bag from the belt. */
int baggage_collect(int student_id)
{
    pthread_mutex_lock(&baggage_mutex);

    for (int i = 0; i < belt_count; i++) {

        if (belt[i].student_id == student_id) {

            for (int j = i; j < belt_count - 1; j++)
                belt[j] = belt[j + 1];

            belt_count--;

            pthread_mutex_unlock(&baggage_mutex);
            return 0;
        }
    }

    pthread_mutex_unlock(&baggage_mutex);

    return -1;
}


/* Actual baggage handler thread. */
static void *baggage_handler(void *arg)
{
    int h = (int)(intptr_t)arg;

    pthread_mutex_lock(&baggage_mutex);

    while (!handler_shutdown) {

        while (handler_state[h] != HANDLER_ASSIGNED && !handler_shutdown)
            pthread_cond_wait(&baggage_cond, &baggage_mutex);

        if (handler_shutdown)
            break;

        /* Handler acknowledges the loading task. */
        handler_state[h] = HANDLER_LOADING;

        pthread_cond_broadcast(&baggage_cond);

        /* Wait for the coordinator's logical-time completion event. */
        while (handler_state[h] == HANDLER_LOADING && !handler_shutdown)
            pthread_cond_wait(&baggage_cond, &baggage_mutex);

        if (handler_shutdown)
            break;

        if (handler_state[h] == HANDLER_DONE) {

            handler_state[h] = HANDLER_IDLE;

            pthread_cond_broadcast(&baggage_cond);
        }
    }

    pthread_mutex_unlock(&baggage_mutex);

    return NULL;
}


/* Start the two handler threads. */
void baggage_start_handlers(void)
{
    pthread_mutex_lock(&baggage_mutex);
    handler_shutdown = 0;
    pthread_mutex_unlock(&baggage_mutex);

    for (int h = 0; h < HANDLER_COUNT; h++) {

        int rc = pthread_create(
            &handlers[h],
            NULL,
            baggage_handler,
            (void *)(intptr_t)h
        );

        if (rc != 0) {

            fprintf(stderr,
                    "Failed to create handler %d (error %d)\n",
                    h + 1, rc);
        }
        else {
            handler_created[h] = 1;
        }
    }
}


/* Stop and join handler threads. */
void baggage_stop_handlers(void)
{
    pthread_mutex_lock(&baggage_mutex);

    handler_shutdown = 1;

    pthread_cond_broadcast(&baggage_cond);

    pthread_mutex_unlock(&baggage_mutex);

    for (int h = 0; h < HANDLER_COUNT; h++) {

        if (handler_created[h]) {

            pthread_join(handlers[h], NULL);
            handler_created[h] = 0;
        }
    }
}


/* Destroy baggage synchronization objects. */
void baggage_destroy(void)
{
    pthread_cond_destroy(&baggage_cond);
    pthread_mutex_destroy(&baggage_mutex);
}
