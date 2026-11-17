#include <stdio.h>
#include "bag.h"

int main(void)
{
    baggage_init();
    baggage_start_handlers();

    // Submit three bags.
    baggage_submit(1);
    baggage_submit(2);
    baggage_submit(3);

    int h1, h2;
    Bag b1, b2;

    // Assign two bags to the two handlers.
    baggage_assign_next(&h1, &b1);
    baggage_assign_next(&h2, &b2);

    printf("Handler %d received Student %d's bag\n",
           h1, b1.student_id);

    printf("Handler %d received Student %d's bag\n",
           h2, b2.student_id);

    // Both handlers should be busy.
    int h3;
    Bag b3;

    if (!baggage_assign_next(&h3, &b3))
        printf("Both handlers are busy. No handler available.\n");

    // Complete the first handler's logical-time task.
    baggage_complete(h1);

    // The first handler should now be available.
    if (baggage_assign_next(&h3, &b3))
    {
        printf("Handler %d is available again; received Student %d's bag\n",
               h3, b3.student_id);
    }

    // Complete remaining tasks.
    baggage_complete(h2);
    baggage_complete(h3);

    baggage_stop_handlers();
    baggage_destroy();

    return 0;
}