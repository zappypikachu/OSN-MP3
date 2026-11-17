import os
import re
import glob
import csv
import argparse
import subprocess
import statistics
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

# ==================================================
# CONFIGURATION
# ==================================================

SOURCE = "main.c"
BINARY = "./bellman_ford"

GRAPH_DIR = "test_graphs"

CSV_V = "experiment_V.csv"
CSV_T = "experiment_T.csv"

OUTPUT_DIR = "plots"

USERNAME = "akshit.singla"

THREADS_FOR_V = 4

THREAD_COUNTS = [1, 2, 4, 8, 12]

REPEATS = 5

VERTEX_CONFIGS = [100, 200, 400, 800, 1600]

FIXED_V = 2000
FIXED_E = 10000


# ==================================================
# COMPILE C PROGRAM
# ==================================================

def compile_program():

    print("\nCompiling C program...")

    subprocess.run(
        ["gcc", SOURCE, "-O2", "-o", BINARY, "-pthread"],
        check=True
    )

    print("Compilation successful!")


# ==================================================
# RUN ONE GRAPH
# ==================================================

def run_graph(filename, threads):

    with open(filename, "r") as f:
        lines = f.readlines()

    # Change thread count in input header
    header = lines[0].split()
    header[3] = str(threads)
    lines[0] = " ".join(header) + "\n"

    input_data = "".join(lines)

    result = subprocess.run(
        [BINARY],
        input=input_data,
        capture_output=True,
        text=True,
        check=True,
        timeout=300
    )

    output = result.stdout + result.stderr

    match = re.search(
        r"Execution Time:\s*([0-9.]+)\s*ms",
        output
    )

    if not match:
        raise ValueError(
            f"Execution time not found for {filename}\n"
            f"Output: {output}"
        )

    return float(match.group(1))


# ==================================================
# EXPERIMENT A: VARY V, FIX THREADS
# ==================================================

def experiment_vertices():

    print("\n========== EXPERIMENT A ==========")

    results = []

    for filename in sorted(
        glob.glob(os.path.join(GRAPH_DIR, "graph_*.txt"))
    ):

        basename = os.path.basename(filename)

        match = re.match(
            r"graph_(\d+)_(\d+)_(\d+)\.txt",
            basename
        )

        if not match:
            continue

        V, E, graph_id = map(int, match.groups())

        if V not in VERTEX_CONFIGS:
            continue

        # Warm-up
        run_graph(filename, THREADS_FOR_V)

        times = []

        for _ in range(REPEATS):

            t = run_graph(filename, THREADS_FOR_V)
            times.append(t)

        avg = statistics.mean(times)

        results.append([
            V, E, THREADS_FOR_V, graph_id, avg
        ])

        print(
            f"V={V}, E={E}, Graph={graph_id}, "
            f"Average={avg:.6f} ms"
        )

    with open(CSV_V, "w", newline="") as f:

        writer = csv.writer(f)

        writer.writerow([
            "V", "E", "Threads",
            "Graph_ID", "Average_Runtime_ms"
        ])

        writer.writerows(results)

    print("\nSaved:", CSV_V)


# ==================================================
# EXPERIMENT B: VARY THREADS, FIX V AND E
# ==================================================

def experiment_threads():

    print("\n========== EXPERIMENT B ==========")

    files = sorted(
        glob.glob(
            os.path.join(
                GRAPH_DIR,
                f"graph_{FIXED_V}_{FIXED_E}_*.txt"
            )
        )
    )

    if not files:
        raise FileNotFoundError(
            f"No graphs found for V={FIXED_V}, E={FIXED_E}"
        )

    results = []

    for T in THREAD_COUNTS:

        for filename in files:

            basename = os.path.basename(filename)

            graph_id = int(
                basename.split("_")[-1].split(".")[0]
            )

            # Warm-up
            run_graph(filename, T)

            times = []

            for _ in range(REPEATS):

                t = run_graph(filename, T)
                times.append(t)

            avg = statistics.mean(times)

            results.append([
                FIXED_V, FIXED_E, T, graph_id, avg
            ])

            print(
                f"T={T}, Graph={graph_id}, "
                f"Average={avg:.6f} ms"
            )

    with open(CSV_T, "w", newline="") as f:

        writer = csv.writer(f)

        writer.writerow([
            "V", "E", "Threads",
            "Graph_ID", "Average_Runtime_ms"
        ])

        writer.writerows(results)

    print("\nSaved:", CSV_T)


# ==================================================
# WATERMARK
# ==================================================

def add_watermark():

    plt.text(
        0.98, 0.96,
        USERNAME,
        transform=plt.gca().transAxes,
        ha="right",
        va="top",
        fontsize=10,
        alpha=0.65
    )


# ==================================================
# READ CSV AND PLOT
# ==================================================

def generate_plots():

    print("\n========== GENERATING PLOTS ==========")

    os.makedirs(OUTPUT_DIR, exist_ok=True)

    # Read uploaded/generated CSV files
    df_V = pd.read_csv(CSV_V)
    df_T = pd.read_csv(CSV_T)

    required = {
        "V", "E", "Threads", "Graph_ID",
        "Average_Runtime_ms"
    }

    if not required.issubset(df_V.columns):
        raise ValueError("Invalid columns in experiment_V.csv")

    if not required.issubset(df_T.columns):
        raise ValueError("Invalid columns in experiment_T.csv")

    # Average across different edge sets
    avg_V = (
        df_V.groupby(["V", "E"])["Average_Runtime_ms"]
        .agg(["mean", "std"])
        .reset_index()
    )

    avg_T = (
        df_T.groupby("Threads")["Average_Runtime_ms"]
        .agg(["mean", "std"])
        .reset_index()
    )

    # ==================================================
    # THEORETICAL CURVE: kVE
    # ==================================================

    x = (avg_V["V"] * avg_V["E"]).to_numpy()
    y = avg_V["mean"].to_numpy()

    # Least squares fitting through origin
    k = np.dot(x, y) / np.dot(x, x)

    avg_V["theoretical"] = k * x

    print(f"\nFitted k = {k:.8e}")
    print("Theoretical curve: Runtime = k * V * E")

    # ==================================================
    # PLOT 1: RUNTIME VS V
    # ==================================================

    plt.figure(figsize=(10, 6))

    plt.errorbar(
        avg_V["V"],
        avg_V["mean"],
        yerr=avg_V["std"],
        marker="o",
        capsize=5,
        linewidth=2,
        label="Experimental Runtime"
    )

    plt.plot(
        avg_V["V"],
        avg_V["theoretical"],
        marker="s",
        linestyle="--",
        linewidth=2,
        label=f"Theoretical kVE (k={k:.3e})"
    )

    plt.xlabel("Number of Vertices (V)")
    plt.ylabel("Runtime (ms)")

    plt.title(
        f"Bellman-Ford Runtime vs V "
        f"(Threads = {THREADS_FOR_V})"
    )

    plt.grid(True, alpha=0.3)
    plt.legend()

    add_watermark()

    plt.tight_layout()

    plt.savefig(
        os.path.join(
            OUTPUT_DIR,
            "runtime_vs_vertices.png"
        ),
        dpi=300
    )

    plt.close()

    # ==================================================
    # PLOT 2: RUNTIME VS THREADS
    # ==================================================

    plt.figure(figsize=(10, 6))

    plt.errorbar(
        avg_T["Threads"],
        avg_T["mean"],
        yerr=avg_T["std"],
        marker="o",
        capsize=5,
        linewidth=2
    )

    plt.xlabel("Number of Threads (T)")
    plt.ylabel("Runtime (ms)")

    plt.title(
        f"Bellman-Ford Runtime vs Threads "
        f"(V={FIXED_V}, E={FIXED_E})"
    )

    plt.xticks(avg_T["Threads"])

    plt.grid(True, alpha=0.3)

    add_watermark()

    plt.tight_layout()

    plt.savefig(
        os.path.join(
            OUTPUT_DIR,
            "runtime_vs_threads.png"
        ),
        dpi=300
    )

    plt.close()

    # ==================================================
    # SAVE AVERAGED DATA
    # ==================================================

    avg_V.to_csv(
        os.path.join(OUTPUT_DIR, "averaged_V.csv"),
        index=False
    )

    avg_T.to_csv(
        os.path.join(OUTPUT_DIR, "averaged_T.csv"),
        index=False
    )

    print("\nPlots generated successfully!")

    print("\nAverage runtime vs V:")
    print(avg_V.to_string(index=False))

    print("\nAverage runtime vs T:")
    print(avg_T.to_string(index=False))

    print(f"\nAll output saved in '{OUTPUT_DIR}/'")


# ==================================================
# MAIN
# ==================================================

if __name__ == "__main__":

    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--run",
        action="store_true",
        help="Run all benchmarks using test_graphs first"
    )

    args = parser.parse_args()

    if args.run:

        compile_program()

        experiment_vertices()

        experiment_threads()

    generate_plots()
