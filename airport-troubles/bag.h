
#ifndef BAGGAGE_H
#define BAGGAGE_H

#define BELT_CAPACITY 2
#define MAX_BAGS 1000
#define HANDLER_COUNT 2

typedef struct {
    int student_id;
} Bag;

void baggage_init(void);

int baggage_submit(int student_id);

int baggage_assign_next(int *handler_id, Bag *bag);

int baggage_load_complete(int handler_id, int *student_id);

int baggage_has_bag(int student_id);

int baggage_collect(int student_id);

void baggage_start_handlers(void);
void baggage_stop_handlers(void);
void baggage_destroy(void);

#endif
