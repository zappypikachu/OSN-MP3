
import subprocess
import csv
import re
import os
import glob
import statistics

# ---------------- CONFIGURATION ----------------

SOURCE = "main.c"
BINARY = "./bellman_ford"

THREADS_FOR_V = 4

THREAD_COUNTS = [1, 2, 4, 8, 12]

REPEATS = 5

# ---------------- COMPILE PROGRAM ----------------

print("Compiling C program...")

subprocess.run(
    ["gcc", SOURCE, "-O2", "-o", BINARY, "-pthread"],
    check=True
)

print("Compilation successful!\n")


# ---------------- RUN ONE GRAPH ----------------

def run_graph(filename, threads):

    with open(filename, "r") as f:
        content = f.read()

    lines = content.splitlines()

    # Change thread count in input header
    header = lines[0].split()
    header[3] = str(threads)

    lines[0] = " ".join(header)

    modified_input = "\n".join(lines) + "\n"

    result = subprocess.run(
        [BINARY],
        input=modified_input,
        capture_output=True,
        text=True,
        check=True
    )

    # Extract execution time printed by C program
    match = re.search(
        r"Execution Time:\s*([0-9.]+)\s*ms",
        result.stdout
    )

    if not match:
        raise ValueError(
            f"Execution time not found for {filename}"
        )

    return float(match.group(1))


# ---------------- EXPERIMENT A ----------------
# Runtime vs V, with fixed thread count

print("Starting Experiment A...")

results_V = []

graph_files = sorted(
    glob.glob("test_graphs/graph_*.txt")
)

for filename in graph_files:

    basename = os.path.basename(filename)

    match = re.match(
        r"graph_(\d+)_(\d+)_(\d+)\.txt",
        basename
    )

    if not match:
        continue

    V = int(match.group(1))
    E = int(match.group(2))
    graph_id = int(match.group(3))

    # Use only graphs for Experiment A
    if V not in [100, 200, 400, 800, 1600]:
        continue

    # Warm-up run
    run_graph(filename, THREADS_FOR_V)

    times = []

    for _ in range(REPEATS):
        t = run_graph(filename, THREADS_FOR_V)
        times.append(t)

    avg = statistics.mean(times)

    results_V.append([
        V, E, THREADS_FOR_V,
        graph_id, avg
    ])

    print(
        f"V={V}, E={E}, Graph={graph_id}, "
        f"Average={avg:.6f} ms"
    )


with open("experiment_V.csv", "w", newline="") as f:

    writer = csv.writer(f)

    writer.writerow([
        "V", "E", "Threads",
        "Graph_ID", "Average_Runtime_ms"
    ])

    writer.writerows(results_V)


print("\nExperiment A completed!")
print("Saved experiment_V.csv\n")


# ---------------- EXPERIMENT B ----------------
# Runtime vs number of threads, fixed V and E

print("Starting Experiment B...")

results_T = []

fixed_graphs = sorted(
    glob.glob("test_graphs/graph_2000_10000_*.txt")
)

for T in THREAD_COUNTS:

    for filename in fixed_graphs:

        graph_id = int(
            filename.split("_")[-1].split(".")[0]
        )

        # Warm-up run
        run_graph(filename, T)

        times = []

        for _ in range(REPEATS):
            t = run_graph(filename, T)
            times.append(t)

        avg = statistics.mean(times)

        results_T.append([
            2000, 10000, T,
            graph_id, avg
        ])

        print(
            f"T={T}, Graph={graph_id}, "
            f"Average={avg:.6f} ms"
        )


with open("experiment_T.csv", "w", newline="") as f:

    writer = csv.writer(f)

    writer.writerow([
        "V", "E", "Threads",
        "Graph_ID", "Average_Runtime_ms"
    ])

    writer.writerows(results_T)


print("\nExperiment B completed!")
print("Saved experiment_T.csv")

print("\nAll benchmarks completed successfully!")