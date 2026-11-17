#ifndef SCHEDULER_H
#define SCHEDULER_H

#define MAX_EVENTS 4000

typedef enum {
    ARRIVAL,
    SCAN_START,
    SCAN_COMPLETE,
    APPROVAL_COMPLETE,
    ENTER_HALL,
    STAMP_COMPLETE,
    EXIT_COMPLETE,
    OFFICER_LEAVE_COMPLETE,
    BAG_LOAD_COMPLETE,
    BAG_COLLECTION_COMPLETE

} EventType;

typedef struct
{
    int time;
    EventType type;
    int actor_index;
    int order;
} Event;

void scheduler_init(void);
int scheduler_add_event(Event event);
int scheduler_pop_next(Event *event);

#endif
