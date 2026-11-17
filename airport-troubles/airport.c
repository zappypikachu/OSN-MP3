#include <stdio.h>
#include <pthread.h>
#include <stdint.h>
#include "scheduler.h"
#include "clock.h"
#include "bag.h"

#define MAXN 1000

typedef enum
{
    NOT_ARRIVED,
    WAITING_FOR_OFFICER,
    APPROVED,

    WAITING_FOR_BAGGAGE,
    COLLECTING_BAGGAGE,

    FINISHED,
    STAMPED,

    EXITING_HALL
} StudentState;

int ti[MAXN];
int role[MAXN];
int role_number[MAXN];

int waiting_students[MAXN];
int waiting_count = 0;

int completed_scans = 0;
int scanner_busy[3] = {0, 0, 0};
int scanner_for_student[MAXN];
StudentState student_state[MAXN];

int students_pending_approval = 0;
// Individual student threads
pthread_t student_threads[MAXN];
Event student_events[MAXN];

int student_event_ready[MAXN] = {0};
int student_event_done[MAXN] = {0};

// Students waiting outside while the hall is closed
int outside_students[MAXN];
int outside_count = 0;
int hall_closed = 0;

// Immigration officer thread and event mailbox
pthread_t officer_thread;
int officer_entry_complete_time = 0;
Event officer_event;
int approval_scheduled = 0;
int officer_index = -1;
int student_count = 0;

pthread_cond_t officer_ready_cond = PTHREAD_COND_INITIALIZER;
int officer_event_ready = 0;
int officer_event_done = 0;

// Officer synchronization
pthread_cond_t officer_event_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t officer_done_cond = PTHREAD_COND_INITIALIZER;
int approval_ready = 0;

// One condition variable per student
pthread_cond_t student_event_cond[MAXN];
pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;

// Used to wait until a student finishes processing an event
pthread_cond_t coordinator_cond = PTHREAD_COND_INITIALIZER;

// Immigration officer synchronization
pthread_cond_t officer_condition = PTHREAD_COND_INITIALIZER;

// Thread termination
int shutdown_requested = 0;

void start_scan(int student_index, int delay)
{
    Event event = {
        .time = clock_get_time() + delay,
        .type = SCAN_START,
        .actor_index = student_index,
        .order = student_index};

    if (scheduler_add_event(event) != 0)
    {
        fprintf(stderr, "Failed to schedule scan start\n");
    }
}

void student_enter_hall(int i, int current_time)
{
    printf("%d Student %d enters\n",
           current_time, role_number[i]);

    students_pending_approval++;

    int scanner = -1;

    // Find an available scanner
    for (int s = 0; s < 3; s++)
    {
        if (!scanner_busy[s])
        {
            scanner = s;
            break;
        }
    }

    if (scanner != -1)
    {
        scanner_busy[scanner] = 1;
        scanner_for_student[i] = scanner;

        start_scan(i, 1);
    }
    else
    {
        waiting_students[waiting_count++] = i;
    }
}

void dispatch_student_event(int index, Event event)
{
    pthread_mutex_lock(&state_mutex);

    student_events[index] = event;
    student_event_ready[index] = 1;

    pthread_cond_signal(&student_event_cond[index]);

    while (!student_event_done[index])
    {
        pthread_cond_wait(&coordinator_cond, &state_mutex);
    }

    student_event_done[index] = 0;

    pthread_mutex_unlock(&state_mutex);
}

void dispatch_officer_event(Event event)
{
    pthread_mutex_lock(&state_mutex);

    officer_event = event;
    officer_event_ready = 1;

    pthread_cond_signal(&officer_event_cond);

    // Wait until the officer processes the event
    while (!officer_event_done)
        pthread_cond_wait(&officer_done_cond, &state_mutex);

    officer_event_done = 0;

    pthread_mutex_unlock(&state_mutex);
}

void schedule_outside_students(void)
{
    pthread_mutex_lock(&state_mutex);

    int current_time = clock_get_time();

    // The officer must have left before students re-enter
    if (hall_closed)
    {
        pthread_mutex_unlock(&state_mutex);
        return;
    }

    // Schedule outside students in FIFO order
    for (int j = 0; j < outside_count; j++)
    {
        int i = outside_students[j];

        Event event = {
            .time = current_time + 1,
            .type = ENTER_HALL,
            .actor_index = i,
            .order = MAXN + 2 + j};

        if (scheduler_add_event(event) != 0)
            fprintf(stderr, "Failed to schedule re-entry for Student %d\n", role_number[i]);
    }

    outside_count = 0;

    pthread_mutex_unlock(&state_mutex);
}

void schedule_stamps(void)
{
    pthread_mutex_lock(&state_mutex);

    int current_time = clock_get_time();

    for (int i = 0; i < student_count; i++)
    {
        if (role[i] == 1 &&
            student_state[i] == APPROVED)
        {
            Event event = {
                .time = current_time + 1,
                .type = STAMP_COMPLETE,
                .actor_index = i,
                .order = 2 * MAXN + i};

            if (scheduler_add_event(event) != 0)
            {
                fprintf(stderr,"Failed to schedule stamp for Student %d\n",role_number[i]);
            }
        }
    }

    pthread_mutex_unlock(&state_mutex);
}

void schedule_baggage_handlers(void)
{
    pthread_mutex_lock(&state_mutex);

    int current_time = clock_get_time();

    while (1)
    {
        int h;
        Bag bag;

        // Assign a ready bag or resume a handler waiting for belt space.
        if (!baggage_assign_next(&h, &bag))
            break;

        // Loading takes one logical time unit.
        Event completion = {
            .time = current_time + 1,
            .type = BAG_LOAD_COMPLETE,
            .actor_index = h,
            .order = 4 * MAXN + h};

        if (scheduler_add_event(completion) != 0)
        {
            fprintf(stderr, "Failed to schedule bag loading\n");
            pthread_mutex_unlock(&state_mutex);
            return;
        }
    }

    pthread_mutex_unlock(&state_mutex);
}

void schedule_approval_if_ready(void)
{
    pthread_mutex_lock(&state_mutex);

    // No approval is needed if the hall is open
    // or approval has already been scheduled
    if (!hall_closed || approval_scheduled ||
        completed_scans < students_pending_approval)
    {
        pthread_mutex_unlock(&state_mutex);
        return;
    }

    // Wait until the officer confirms readiness
    while (!approval_ready && !shutdown_requested)
    {
        pthread_cond_wait(&officer_ready_cond, &state_mutex);
    }

    if (shutdown_requested)
    {
        pthread_mutex_unlock(&state_mutex);
        return;
    }

    approval_scheduled = 1;

    int approval_time = clock_get_time();

    if (approval_time < officer_entry_complete_time)
        approval_time = officer_entry_complete_time;

    Event event = {
        .time = approval_time,
        .type = APPROVAL_COMPLETE,
        .actor_index = officer_index,
        .order = MAXN + 1};

    if (scheduler_add_event(event) != 0)
        fprintf(stderr, "Failed to schedule approval completion\n");

    pthread_mutex_unlock(&state_mutex);
}
void *student(void *arg)
{
    int i = (intptr_t)arg;

    while (1)
    {
        Event event;
        pthread_mutex_lock(&state_mutex);
        // Wait until this student receives an event
        while (!student_event_ready[i] && !shutdown_requested)
            pthread_cond_wait(&student_event_cond[i], &state_mutex);

        if (shutdown_requested && !student_event_ready[i])
        {
            pthread_mutex_unlock(&state_mutex);
            break;
        }

        // Collect this student's event
        event = student_events[i];
        student_event_ready[i] = 0;

        int current_time = clock_get_time();

        if (event.type == ARRIVAL)
        {
            if (hall_closed)
            {
                outside_students[outside_count++] = i;
            }
            else
                student_enter_hall(i, current_time);
        }

        else if (event.type == ENTER_HALL)
        {
            if (hall_closed)
            {
                outside_students[outside_count++] = i;
            }
            else
                student_enter_hall(i, current_time);
        }

        else if (event.type == SCAN_START)
        {
            printf("%d Student %d scans passport\n", current_time, role_number[i]);

            // Schedule completion after 2 time units
            Event completion = {
                .time = current_time + 2,
                .type = SCAN_COMPLETE,
                .actor_index = i,
                .order = i};

            if (scheduler_add_event(completion) != 0)
                fprintf(stderr, "Failed to schedule scan completion\n");
        }

        else if (event.type == SCAN_COMPLETE)
        {
            student_state[i] = WAITING_FOR_OFFICER;
            completed_scans++;
            // Notify the officer that another student finished scanning
            pthread_cond_signal(&officer_condition);

            // Release this student's scanner
            int scanner = scanner_for_student[i];

            if (scanner >= 0 && scanner < 3)
            {
                scanner_busy[scanner] = 0;
                scanner_for_student[i] = -1;

                // Check if another student is waiting
                if (waiting_count > 0)
                {
                    int next_student = waiting_students[0];

                    // Remove the first waiting student
                    for (int j = 0; j < waiting_count - 1; j++)
                        waiting_students[j] = waiting_students[j + 1];
                    waiting_count--;

                    // Assign the released scanner
                    scanner_busy[scanner] = 1;
                    scanner_for_student[next_student] = scanner;

                    // Next student starts immediately
                    start_scan(next_student, 0);
                }
            }
            else
            {
                fprintf(stderr,
                        "Error: Student %d has no assigned scanner\n",
                        role_number[i]);
            }
        }

        else if (event.type == STAMP_COMPLETE)
        {
            printf("%d Student %d receives stamp\n", current_time, role_number[i]);
            student_state[i] = STAMPED;

            // Submit the bag to the handler's ready queue.
            if (baggage_submit(role_number[i]) != 0)
                fprintf(stderr, "Failed to submit bag for Student %d\n", role_number[i]);
            else if (!hall_closed)
            {
                // Student can start leaving the hall.
                student_state[i] = EXITING_HALL;

                Event exit_event = {
                    .time = current_time + 1,
                    .type = EXIT_COMPLETE,
                    .actor_index = i,
                    .order = 3 * MAXN + i};

                if (scheduler_add_event(exit_event) != 0)
                {
                    fprintf(stderr, "Failed to schedule student exit\n");
                    student_state[i] = STAMPED;
                }
            }
        }

        else if (event.type == EXIT_COMPLETE)
        {
            if (student_state[i] == EXITING_HALL)
            {
                if (hall_closed)
                {
                    student_state[i] = STAMPED;
                }
                else
                {
                    printf("%d Student %d leaves hall\n", current_time, role_number[i]);

                    student_state[i] = WAITING_FOR_BAGGAGE;

                    if (baggage_has_bag(role_number[i]))
                    {
                        Event collect_event = {
                            .time = current_time + 1,
                            .type = BAG_COLLECTION_COMPLETE,
                            .actor_index = i,
                            .order = 5 * MAXN + i};

                        if (scheduler_add_event(collect_event) == 0)
                            student_state[i] = COLLECTING_BAGGAGE;
                    }
                }
            }
        }
        else if (event.type == BAG_COLLECTION_COMPLETE)
        {
            if (student_state[i] == COLLECTING_BAGGAGE)
            {
                if (baggage_collect(role_number[i]) == 0)
                {
                    printf("%d Student %d collects bag\n", current_time, role_number[i]);
                    student_state[i] = FINISHED;
                }
                else
                {
                    fprintf(stderr, "Bag not found for Student %d\n", role_number[i]);
                    student_state[i] = WAITING_FOR_BAGGAGE;
                }
            }
        }
        student_event_done[i] = 1;
        pthread_cond_signal(&coordinator_cond);
        pthread_mutex_unlock(&state_mutex);
    }

    return NULL;
}

void *officer(void *arg)
{
    (void)arg;

    while (1)
    {
        pthread_mutex_lock(&state_mutex);

        // Wait for an event from the coordinator
        while (!officer_event_ready && !shutdown_requested)
            pthread_cond_wait(&officer_event_cond, &state_mutex);

        // Terminate when shutdown is requested
        if (shutdown_requested && !officer_event_ready)
        {
            pthread_mutex_unlock(&state_mutex);
            break;
        }

        // Collect the event
        Event event = officer_event;
        officer_event_ready = 0;

        // Officer enters the hall
        if (event.type == ARRIVAL)
        {
            hall_closed = 1;
            officer_index = event.actor_index;
            officer_entry_complete_time = clock_get_time() + 1;

            printf("%d Immigration officer %d enters\n", clock_get_time(), role_number[event.actor_index]);

            // Acknowledge arrival
            officer_event_done = 1;
            pthread_cond_signal(&officer_done_cond);

            // Wait until every student inside has completed scanning
            while (completed_scans < students_pending_approval && !shutdown_requested)
                pthread_cond_wait(&officer_condition, &state_mutex);

            if (!shutdown_requested)
            {
                approval_ready = 1;
                // Notify the coordinator
                pthread_cond_signal(&officer_ready_cond);
            }
        }

        // Officer completes batch approval

        else if (event.type == APPROVAL_COMPLETE)
        {
            printf("%d Immigration officer %d approves\n", clock_get_time(), role_number[event.actor_index]);
            // Approve all students in the current batch.
            for (int i = 0; i < student_count; i++)
                if (role[i] == 1 && student_state[i] == WAITING_FOR_OFFICER)
                    student_state[i] = APPROVED;

            students_pending_approval = 0;
            completed_scans = 0;

            // Officer departure takes one logical time unit.
            Event leave_event = {
                .time = clock_get_time() + 1,
                .type = OFFICER_LEAVE_COMPLETE,
                .actor_index = event.actor_index,
                .order = -1};

            if (scheduler_add_event(leave_event) != 0)
                fprintf(stderr, "Failed to schedule officer departure\n");

            officer_event_done = 1;
            pthread_cond_signal(&officer_done_cond);
        }

        else if (event.type == OFFICER_LEAVE_COMPLETE)
        {
            printf("%d Immigration officer %d leaves\n", clock_get_time(), role_number[event.actor_index]);

            hall_closed = 0;
            approval_ready = 0;
            approval_scheduled = 0;
            officer_event_done = 1;
            pthread_cond_signal(&officer_done_cond);
        }

        pthread_mutex_unlock(&state_mutex);
    }

    return NULL;
}

int main()
{
    int n;
    if (scanf("%d", &n) != 1)
    {
        fprintf(stderr, "Invalid number of input lines\n");
        return 1;
    }
    if (n < 0 || n > MAXN)
    {
        fprintf(stderr, "Invalid number of input lines\n");
        return 1;
    }
    student_count = n;
    for (int i = 0; i < n; i++)
    {
        scanf("%d", &ti[i]);
        char r[20];
        scanf("%s", r);
        if (r[0] == 'I')
        {
            scanf("%s", r);
            role[i] = 2;
        }
        else
            role[i] = 1;

        scanf("%d", &role_number[i]);
    }

    for (int i = 0; i < n; i++)
    {
        if (role[i] == 1)
        {
            student_state[i] = NOT_ARRIVED;
            scanner_for_student[i] = -1;

            pthread_cond_init(&student_event_cond[i], NULL);
        }
    }

    // Create one thread for every student
    for (int i = 0; i < n; i++)
    {
        if (role[i] == 1)
        {
            int result = pthread_create(&student_threads[i], NULL, student, (void *)(intptr_t)i);

            if (result != 0)
            {
                fprintf(stderr, "Failed to create thread for Student %d\n", role_number[i]);
                return 1;
            }
        }
    }

    int result = pthread_create(&officer_thread, NULL, officer, NULL);
    if (result != 0)
    {
        fprintf(stderr, "Failed to create officer thread\n");
        return 1;
    }

    scheduler_init();
    clock_init(0);

    baggage_init();
    baggage_start_handlers();

    for (int i = 0; i < n; i++)
    {
        Event event = {
            .time = ti[i],
            .type = ARRIVAL,
            .actor_index = i,
            .order = i};

        if (scheduler_add_event(event) != 0)
        {
            fprintf(stderr, "Failed to add arrival event\n");
            return 1;
        }
    }

    Event event;

    while (1)
    {
        /*
         * Scheduler access is protected because student and officer
         * threads can add events concurrently.
         */
        pthread_mutex_lock(&state_mutex);

        int result = scheduler_pop_next(&event);

        pthread_mutex_unlock(&state_mutex);

        if (result != 0)
            break;

        // Advance logical time.
        if (clock_advance_to(event.time) != 0)
        {
            fprintf(stderr, "Invalid event time\n");
            return 1;
        }

        int i = event.actor_index;

        switch (event.type)
        {
        case ARRIVAL:

            if (role[i] == 1)
            {
                dispatch_student_event(i, event);
                schedule_baggage_handlers();
            }
            else if (role[i] == 2)
            {
                dispatch_officer_event(event);
            }

            break;

        case SCAN_START:
        case SCAN_COMPLETE:
        case ENTER_HALL:
        case STAMP_COMPLETE:
        case EXIT_COMPLETE:
        case BAG_COLLECTION_COMPLETE:

            dispatch_student_event(i, event);

            // A student may have submitted a bag or freed a belt slot.
            schedule_baggage_handlers();

            break;

        case APPROVAL_COMPLETE:

            dispatch_officer_event(event);

            // Start stamping the approved students.
            schedule_stamps();

            break;

        case OFFICER_LEAVE_COMPLETE:

            dispatch_officer_event(event);

            // Allow students waiting outside to enter.
            schedule_outside_students();

            // Schedule exits for students already stamped.
            pthread_mutex_lock(&state_mutex);

            for (int j = 0; j < student_count; j++)
            {
                if (role[j] == 1 &&
                    student_state[j] == STAMPED)
                {
                    student_state[j] = EXITING_HALL;

                    Event exit_event = {
                        .time = clock_get_time() + 1,
                        .type = EXIT_COMPLETE,
                        .actor_index = j,
                        .order = 3 * MAXN + j};

                    if (scheduler_add_event(exit_event) != 0)
                    {
                        fprintf(stderr,
                                "Failed to schedule exit for Student %d\n",
                                role_number[j]);

                        student_state[j] = STAMPED;
                    }
                }
            }

            pthread_mutex_unlock(&state_mutex);

            break;

        case BAG_LOAD_COMPLETE:
        {
            int h = event.actor_index;
            int student_id = -1;
            int student_index = -1;

            if (h < 0 || h >= HANDLER_COUNT)
            {
                fprintf(stderr, "Invalid handler ID\n");
                break;
            }

            // Complete loading and place bag on the belt.
            if (baggage_load_complete(h, &student_id) != 0)
            {
                fprintf(stderr,
                        "Bag loading failed for Handler %d\n", h + 1);
                break;
            }
            printf("%d Baggage handler %d loads bag for Student %d\n",clock_get_time(), h + 1, student_id);

            pthread_mutex_lock(&state_mutex);

            // Find the owner.
            for (int j = 0; j < student_count; j++)
            {
                if (role[j] == 1 &&
                    role_number[j] == student_id)
                {
                    student_index = j;
                    break;
                }
            }

            // If the student has already left the hall,
            // schedule collection.
            if (student_index != -1 &&
                student_state[student_index] == WAITING_FOR_BAGGAGE)
            {
                Event collect_event = {
                    .time = clock_get_time() + 1,
                    .type = BAG_COLLECTION_COMPLETE,
                    .actor_index = student_index,
                    .order = 5 * MAXN + student_index};

                if (scheduler_add_event(collect_event) == 0)
                    student_state[student_index] = COLLECTING_BAGGAGE;
            }

            pthread_mutex_unlock(&state_mutex);

            // Try assigning more bags to available handlers.
            schedule_baggage_handlers();

            break;
        }

        default:

            fprintf(stderr, "Unknown event type\n");
            break;
        }

        // Check whether the officer can approve the current batch.
        schedule_approval_if_ready();
    }

    // Tell all student threads to terminate
    // Tell all threads to terminate
    pthread_mutex_lock(&state_mutex);

    shutdown_requested = 1;

    // Wake all student threads
    for (int i = 0; i < student_count; i++)
    {
        if (role[i] == 1)
        {
            pthread_cond_signal(&student_event_cond[i]);
        }
    }

    // Wake the officer thread regardless of what it is waiting for
    pthread_cond_signal(&officer_event_cond);
    pthread_cond_broadcast(&officer_condition);
    pthread_cond_broadcast(&officer_ready_cond);

    pthread_mutex_unlock(&state_mutex);

    // Join student threads
    for (int i = 0; i < student_count; i++)
    {
        if (role[i] == 1)
        {
            pthread_join(student_threads[i], NULL);
            pthread_cond_destroy(&student_event_cond[i]);
        }
    }

    // Join officer thread
    pthread_join(officer_thread, NULL);

    baggage_stop_handlers();
    baggage_destroy();

    return 0;
}
