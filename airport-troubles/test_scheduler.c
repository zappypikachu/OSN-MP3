
#include <stdio.h>
#include "scheduler.h"

int main(void) {
    scheduler_init();

    scheduler_add_event((Event){5, ARRIVAL, 0, 0});
    scheduler_add_event((Event){2, ARRIVAL, 1, 1});
    scheduler_add_event((Event){5, SCAN_COMPLETE, 2, 2});

    Event event;

    while (scheduler_pop_next(&event) == 0) {
        printf("Time %d: ", event.time);

        if (event.type == ARRIVAL) {
            printf("Arrival");
        } else {
            printf("Scan complete");
        }

        printf(", actor index %d\n", event.actor_index);
    }

    return 0;
}
