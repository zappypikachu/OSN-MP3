
import random
import os

def generate_graph(V, E, seed):
    random.seed(seed)

    edges = set()

    # Step 1: Generate a random spanning tree.
    # This guarantees that the graph is connected.
    for v in range(1, V):
        u = random.randint(0, v - 1)
        edges.add((u, v))

    # Step 2: Add random edges until we reach E.
    while len(edges) < E:
        u = random.randint(0, V - 1)
        v = random.randint(0, V - 1)

        if u != v:
            edge = (min(u, v), max(u, v))
            edges.add(edge)

    # Step 3: Shuffle edge order.
    edges = list(edges)
    random.shuffle(edges)

    return edges


def save_graph(V, E, T, graph_id):
    seed = graph_id * 1000 + V

    edges = generate_graph(V, E, seed)

    os.makedirs("test_graphs", exist_ok=True)

    filename = f"test_graphs/graph_{V}_{E}_{graph_id}.txt"

    with open(filename, "w") as f:
        # Source vertex = 0
        f.write(f"{V} {E} 0 {T}\n")

        for u, v in edges:
            f.write(f"{u} {v}\n")

    print("Generated:", filename)


# Experiment A: Vary V and E.
vertex_configs = [
    (100, 500),
    (200, 1000),
    (400, 2000),
    (800, 4000),
    (1600, 8000),
    (2000, 10000    )
]

THREADS = 4
GRAPHS_PER_CONFIG = 5

for V, E in vertex_configs:
    for graph_id in range(1, GRAPHS_PER_CONFIG + 1):
        save_graph(V, E, THREADS, graph_id)

print("\nAll graphs generated successfully!")
