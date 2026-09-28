#!/usr/bin/env python3

import csv
import os
import re
import subprocess

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOC_SIM = os.path.join(PROJECT_ROOT, "build", "soc_sim")
RESULTS_DIR = os.path.join(PROJECT_ROOT, "results")

os.makedirs(RESULTS_DIR, exist_ok=True)

# Fixed configuration chosen from Phase 19:
CPU_QOS = 8
DMA_QOS = 4
DNN_QOS = 12

DMA_OUTSTANDING = 16
DNN_OUTSTANDING = 32
AXI_QUEUE_DEPTH = 16

# Focused architectural sweep.
CPU_OUTSTANDING_VALUES = [2, 4, 8, 16]

DSE_CSV = os.path.join(RESULTS_DIR, "outstanding_dse.csv")
PARETO_CSV = os.path.join(RESULTS_DIR, "outstanding_pareto.csv")
PARETO_PNG = os.path.join(RESULTS_DIR, "outstanding_pareto.png")


def run_configuration(cpu_outstanding):
    command = [
        SOC_SIM,
        "--dse",
        str(CPU_QOS),
        str(DMA_QOS),
        str(DNN_QOS),
        str(cpu_outstanding),
        str(DMA_OUTSTANDING),
        str(DNN_OUTSTANDING),
        str(AXI_QUEUE_DEPTH),
    ]

    print()
    print("=" * 72)
    print(
        f"CPU outstanding = {cpu_outstanding} | "
        f"CPU QoS = {CPU_QOS} | "
        f"DMA QoS = {DMA_QOS} | "
        f"DNN QoS = {DNN_QOS}"
    )
    print("=" * 72)

    result = subprocess.run(
        command,
        cwd=PROJECT_ROOT,
        capture_output=True,
        text=True,
    )

    print(result.stdout)

    if result.returncode != 0:
        print(result.stderr)
        raise RuntimeError(
            f"soc_sim failed for CPU outstanding={cpu_outstanding}"
        )

    match = re.search(
        r"PHASE18_RESULT\s+(.+)",
        result.stdout
    )

    if not match:
        raise RuntimeError(
            f"Could not find PHASE18_RESULT for CPU outstanding={cpu_outstanding}"
        )

    fields = {}

    for token in match.group(1).split():
        if "=" not in token:
            continue

        key, value = token.split("=", 1)
        fields[key] = value

    return {
        "cpu_qos": int(fields["cpu_qos"]),
        "dma_qos": int(fields["dma_qos"]),
        "dnn_qos": int(fields["dnn_qos"]),
        "cpu_outstanding": int(fields["cpu_outstanding"]),
        "dma_outstanding": int(fields["dma_outstanding"]),
        "dnn_outstanding": int(fields["dnn_outstanding"]),
        "axi_queue_depth": int(fields["axi_queue_depth"]),

        "cpu_avg_ns": float(fields["cpu_avg_ns"]),
        "cpu_p50_ns": float(fields["cpu_p50_ns"]),
        "cpu_p95_ns": float(fields["cpu_p95_ns"]),
        "cpu_p99_ns": float(fields["cpu_p99_ns"]),
        "cpu_max_ns": float(fields["cpu_max_ns"]),

        "accelerator_throughput_gbps": float(
            fields["accelerator_throughput_gbps"]
        ),
    }


def dominates(a, b):
    """
    A dominates B if:
      - A is no worse in CPU P99 latency
      - A is no worse in accelerator throughput
      - A is strictly better in at least one objective

    Objectives:
      minimize CPU P99 latency
      maximize accelerator throughput
    """

    no_worse_latency = a["cpu_p99_ns"] <= b["cpu_p99_ns"]
    no_worse_throughput = (
        a["accelerator_throughput_gbps"]
        >= b["accelerator_throughput_gbps"]
    )

    strictly_better = (
        a["cpu_p99_ns"] < b["cpu_p99_ns"]
        or
        a["accelerator_throughput_gbps"]
        > b["accelerator_throughput_gbps"]
    )

    return (
        no_worse_latency
        and no_worse_throughput
        and strictly_better
    )


def compute_pareto(results):
    pareto = []

    for candidate in results:
        dominated = False

        for other in results:
            if candidate is other:
                continue

            if dominates(other, candidate):
                dominated = True
                break

        if not dominated:
            pareto.append(candidate)

    # Remove duplicate objective points.
    unique = {}

    for point in pareto:
        key = (
            round(point["cpu_p99_ns"], 6),
            round(point["accelerator_throughput_gbps"], 6),
        )

        if key not in unique:
            unique[key] = point

    pareto = list(unique.values())

    pareto.sort(
        key=lambda x: (
            x["cpu_p99_ns"],
            -x["accelerator_throughput_gbps"],
        )
    )

    return pareto


def write_csv(path, rows):
    if not rows:
        return

    fieldnames = list(rows[0].keys())

    with open(path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def write_pareto_plot(pareto):
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print()
        print("WARNING: matplotlib is not installed.")
        print("CSV files were generated, but the PNG was skipped.")
        return

    if not pareto:
        return

    x = [
        p["cpu_p99_ns"]
        for p in pareto
    ]

    y = [
        p["accelerator_throughput_gbps"]
        for p in pareto
    ]

    labels = [
        f"CPU outstanding={p['cpu_outstanding']}"
        for p in pareto
    ]

    plt.figure(figsize=(8, 6))

    plt.plot(
        x,
        y,
        marker="o",
        linewidth=1.5,
    )

    for xi, yi, label in zip(x, y, labels):
        plt.annotate(
            label,
            (xi, yi),
            xytext=(6, 6),
            textcoords="offset points",
        )

    plt.xlabel("CPU P99 latency (ns)")
    plt.ylabel("Accelerator throughput (GB/s)")
    plt.title(
        "CPU Outstanding Transactions vs "
        "CPU Tail Latency / Accelerator Throughput"
    )

    plt.grid(True, alpha=0.3)
    plt.tight_layout()

    plt.savefig(
        PARETO_PNG,
        dpi=200,
        bbox_inches="tight",
    )

    plt.close()


def main():
    print()
    print("=" * 72)
    print("PHASE 20: CPU OUTSTANDING / PARETO DSE")
    print("=" * 72)

    print(f"CPU QoS        : {CPU_QOS}")
    print(f"DMA QoS        : {DMA_QOS}")
    print(f"DNN QoS        : {DNN_QOS}")
    print(f"DMA outstanding: {DMA_OUTSTANDING}")
    print(f"DNN outstanding: {DNN_OUTSTANDING}")
    print(f"AXI queue      : {AXI_QUEUE_DEPTH}")
    print(
        "CPU outstanding sweep:",
        CPU_OUTSTANDING_VALUES
    )

    results = []

    for cpu_outstanding in CPU_OUTSTANDING_VALUES:
        result = run_configuration(cpu_outstanding)
        results.append(result)

        print(
            f"[RESULT] CPU outstanding={cpu_outstanding:2d} | "
            f"P99={result['cpu_p99_ns']:.2f} ns | "
            f"Accelerator throughput="
            f"{result['accelerator_throughput_gbps']:.2f} GB/s"
        )

    write_csv(DSE_CSV, results)

    pareto = compute_pareto(results)

    write_csv(PARETO_CSV, pareto)

    write_pareto_plot(pareto)

    print()
    print("=" * 72)
    print("PHASE 20 SUMMARY")
    print("=" * 72)

    print(f"Configurations evaluated : {len(results)}")
    print(f"Pareto configurations    : {len(pareto)}")
    print()

    print(
        f"{'CPU OUT':>8} "
        f"{'P99 (ns)':>12} "
        f"{'ACCEL BW (GB/s)':>18}"
    )

    print("-" * 42)

    for r in results:
        print(
            f"{r['cpu_outstanding']:>8} "
            f"{r['cpu_p99_ns']:>12.2f} "
            f"{r['accelerator_throughput_gbps']:>18.2f}"
        )

    print()
    print("PARETO FRONTIER")
    print("-" * 42)

    for r in pareto:
        print(
            f"CPU outstanding={r['cpu_outstanding']:2d} | "
            f"P99={r['cpu_p99_ns']:.2f} ns | "
            f"Accelerator throughput="
            f"{r['accelerator_throughput_gbps']:.2f} GB/s"
        )

    print()
    print(f"DSE results      : {DSE_CSV}")
    print(f"Pareto results   : {PARETO_CSV}")
    print(f"Pareto plot      : {PARETO_PNG}")
    print("=" * 72)


if __name__ == "__main__":
    main()
