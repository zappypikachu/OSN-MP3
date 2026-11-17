#ifndef CLOCK_H
#define CLOCK_H

void clock_init(int start_time);
int clock_get_time(void);
int clock_advance_to(int target_time);

#endif
