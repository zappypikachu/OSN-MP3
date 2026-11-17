#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <pthread.h>
#include <time.h>

#define INF INT_MAX

typedef struct
{
    int src;
    int dest;
    int weight;
} Edge;

typedef struct
{
    int thread_id;
    int start;
    int end;
    Edge *edges;
    int *dist;
    int *new_dist;

    pthread_mutex_t *vertex_locks;
    int *changed;

} ThreadArgs;

void *relax_edges(void *arg)
{

    ThreadArgs *args = (ThreadArgs *)arg;

    // Process assigned edges
    for (int j = args->start; j < args->end; j++)
    {

        int u = args->edges[j].src;
        int v = args->edges[j].dest;
        int w = args->edges[j].weight;

        // Relax u -> v
        pthread_mutex_lock(&args->vertex_locks[v]);

        if (args->dist[u] != INF && args->dist[u] + w < args->new_dist[v])
        {
            args->new_dist[v] = args->dist[u] + w;
            *(args->changed) = 1;
        }

        pthread_mutex_unlock(&args->vertex_locks[v]);

        // Relax v -> u (undirected graph)
        pthread_mutex_lock(&args->vertex_locks[u]);

        if (args->dist[v] != INF && args->dist[v] + w < args->new_dist[u])
        {
            args->new_dist[u] = args->dist[v] + w;
            *(args->changed) = 1;
        }

        pthread_mutex_unlock(&args->vertex_locks[u]);
    }

    return NULL;
}
int main()
{
    int V, E, N, T;

    // Read number of vertices, edges, source
    scanf("%d %d %d %d", &V, &E, &N, &T);

    // Allocate edge array
    Edge *edges = malloc(E * sizeof(Edge));

    // Read edges (undirected graph)
    for (int i = 0; i < E; i++)
    {
        int u, v;

        scanf("%d %d", &u, &v);

        edges[i].src = u;
        edges[i].dest = v;
        edges[i].weight = 1;
    }

    // Allocate distance arrays
    int *dist = malloc(V * sizeof(int));
    int *new_dist = malloc(V * sizeof(int));

    // Initialize distances
    for (int i = 0; i < V; i++)
    {
        dist[i] = INF;
        new_dist[i] = INF;
    }

    dist[N] = 0;
    new_dist[N] = 0;

    // Allocate one mutex for every vertex
    pthread_mutex_t *vertex_locks = malloc(V * sizeof(pthread_mutex_t));

    // Initialize all vertex mutexes
    for (int i = 0; i < V; i++)
        pthread_mutex_init(&vertex_locks[i], NULL);

    // Allocate thread resources
    pthread_t *threads = malloc(T * sizeof(pthread_t));
    ThreadArgs *args = malloc(T * sizeof(ThreadArgs));
    int *thread_changed = calloc(T, sizeof(int));

    struct timespec start, end;

    clock_gettime(CLOCK_MONOTONIC, &start);

    // Bellman-Ford algorithm
    for (int i = 0; i < V - 1; i++)
    {

        // Copy previous distances
        for (int j = 0; j < V; j++)
        {
            new_dist[j] = dist[j];
        }

        // Reset change indicators
        for (int t = 0; t < T; t++)
        {
            thread_changed[t] = 0;
        }

        // Divide edges among threads
        for (int t = 0; t < T; t++)
        {

            int start = (long long)t * E / T;
            int end = (long long)(t + 1) * E / T;

            // Prepare arguments for this thread
            args[t].thread_id = t;
            args[t].start = start;
            args[t].end = end;

            args[t].edges = edges;
            args[t].dist = dist;
            args[t].new_dist = new_dist;

            args[t].vertex_locks = vertex_locks;
            args[t].changed = &thread_changed[t];

            // Create thread
            pthread_create(&threads[t], NULL, relax_edges, &args[t]);
        }

        // Wait for all threads to finish
        for (int t = 0; t < T; t++)
            pthread_join(threads[t], NULL);

        // Check whether any thread updated a distance
        int updated = 0;

        for (int t = 0; t < T; t++)
        {
            if (thread_changed[t])
            {
                updated = 1;
                break;
            }
        }

        // Copy updated distances for next iteration
        for (int j = 0; j < V; j++)
            dist[j] = new_dist[j];

        // Early termination
        if (!updated)
            break;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    // Calculate execution time
    double time_taken;

    time_taken = (end.tv_sec - start.tv_sec) * 1e3;
    time_taken += (end.tv_nsec - start.tv_nsec) / 1e6;

    // Print execution time
    printf("\nExecution Time: %.6f ms\n", time_taken);

    // Print shortest distances
    for (int i = 0; i < V; i++)
    {
        if (dist[i] == INF)
        {
            printf("-1");
        }
        else
        {
            printf("%d", dist[i]);
        }

        if (i != V - 1)
        {
            printf(" ");
        }
    }

    printf("\n");

    free(edges);
    free(dist);
    free(new_dist);

    free(threads);
    free(args);
    free(thread_changed);

    for (int i = 0; i < V; i++)
        pthread_mutex_destroy(&vertex_locks[i]);
    free(vertex_locks);
    return 0;
}
