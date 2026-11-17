#include "scheduler.h"

static Event events[MAX_EVENTS];
static int event_count = 0;

void scheduler_init(void) {
    event_count = 0;
}

int scheduler_add_event(Event event) {
    if (event_count >= MAX_EVENTS) {
        return -1;
    }

    events[event_count++] = event;
    return 0;
}

int scheduler_pop_next(Event *event) {
    if (event_count == 0 || event == 0) {
        return -1;
    }

    int best = 0;

    for (int i = 1; i < event_count; i++) {
        if (events[i].time < events[best].time ||
            (events[i].time == events[best].time &&
             events[i].order < events[best].order)) {
            best = i;
        }
    }

    *event = events[best];

    for (int i = best; i < event_count - 1; i++) {
        events[i] = events[i + 1];
    }

    event_count--;
    return 0;
}
