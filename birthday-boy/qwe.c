#include <stdio.h>
#include <time.h>

int main() {
    struct timespec start, end;
    long long sum = 0;
    long long limit = 1000000;

    // Start timing
    clock_gettime(CLOCK_MONOTONIC, &start);

    // Loop execution
    for (long long i = 0; i <= limit; i++) {
        sum += i;
    }

    // End timing
    clock_gettime(CLOCK_MONOTONIC, &end);

    double time_taken = (end.tv_sec - start.tv_sec) + 
                        (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("The sum is: %lld\n", sum);
    printf("Execution time: %.6f seconds\n", time_taken);

    return 0;
}
