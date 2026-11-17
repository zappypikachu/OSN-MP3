#include <pthread.h>
#include "clock.h"

static int current_time = 0;
static pthread_mutex_t clock_mutex = PTHREAD_MUTEX_INITIALIZER;

void clock_init(int start_time) {
    pthread_mutex_lock(&clock_mutex);
    current_time = start_time;
    pthread_mutex_unlock(&clock_mutex);
}

int clock_get_time(void) {
    pthread_mutex_lock(&clock_mutex);
    int time = current_time;
    pthread_mutex_unlock(&clock_mutex);

    return time;
}

int clock_advance_to(int target_time) {
    pthread_mutex_lock(&clock_mutex);

    if (target_time < current_time) {
        pthread_mutex_unlock(&clock_mutex);
        return -1;
    }

    current_time = target_time;

    pthread_mutex_unlock(&clock_mutex);
    return 0;
}