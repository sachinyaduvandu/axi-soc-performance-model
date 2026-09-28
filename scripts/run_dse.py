#!/usr/bin/env python3

import csv
import itertools
import os
import re
import subprocess
import sys


# ============================================================
# Configuration
# ============================================================

PROJECT_ROOT = os.path.dirname(
    os.path.dirname(os.path.abspath(__file__))
)

SIMULATOR = os.path.join(
    PROJECT_ROOT,
    "build",
    "soc_sim"
)

RESULTS_DIR = os.path.join(
    PROJECT_ROOT,
    "results"
)

DSE_CSV = os.path.join(
    RESULTS_DIR,
    "dse_results.csv"
)

PARETO_CSV = os.path.join(
    RESULTS_DIR,
    "pareto_frontier.csv"
)

PARETO_PNG = os.path.join(
    RESULTS_DIR,
    "pareto_frontier.png"
)


# ============================================================
# Fixed architecture parameters
# ============================================================

CPU_OUTSTANDING = 8
DMA_OUTSTANDING = 16
DNN_OUTSTANDING = 32
AXI_QUEUE_DEPTH = 16


# ============================================================
# DSE sweep space
#
# We deliberately keep this compact:
#
# CPU QoS : 8, 12, 15
# DMA QoS : 4,  8, 12
# DNN QoS : 4,  8, 12
#
# Total = 3 x 3 x 3 = 27 simulations
# ============================================================

CPU_QOS_VALUES = [8, 12, 15]
DMA_QOS_VALUES = [4, 8, 12]
DNN_QOS_VALUES = [4, 8, 12]


# ============================================================
# PHASE18_RESULT parser
# ============================================================

def parse_result(output):
    """
    Parse the machine-readable PHASE18_RESULT line.

    Example:

    PHASE18_RESULT policy=qos
        cpu_qos=15
        dma_qos=8
        dnn_qos=4
        cpu_outstanding=8
        dma_outstanding=16
        dnn_outstanding=32
        axi_queue_depth=16
        cpu_avg_ns=788.80
        cpu_p50_ns=822.00
        cpu_p95_ns=1317.50
        cpu_p99_ns=1400.30
        cpu_max_ns=1421.00
        accelerator_throughput_gbps=9.95
    """

    match = re.search(
        r"PHASE18_RESULT\s+(.+)",
        output
    )

    if not match:
        return None

    fields = {}

    tokens = match.group(1).split()

    for token in tokens:

        if "=" not in token:
            continue

        key, value = token.split(
            "=",
            1
        )

        fields[key] = value

    numeric_float_fields = [
        "cpu_avg_ns",
        "cpu_p50_ns",
        "cpu_p95_ns",
        "cpu_p99_ns",
        "cpu_max_ns",
        "accelerator_throughput_gbps"
    ]

    numeric_int_fields = [
        "cpu_qos",
        "dma_qos",
        "dnn_qos",
        "cpu_outstanding",
        "dma_outstanding",
        "dnn_outstanding",
        "axi_queue_depth"
    ]

    for key in numeric_float_fields:

        if key not in fields:
            return None

        fields[key] = float(fields[key])

    for key in numeric_int_fields:

        if key not in fields:
            return None

        fields[key] = int(fields[key])

    fields["policy"] = fields.get(
        "policy",
        "qos"
    )

    return fields


# ============================================================
# Run one simulation
# ============================================================

def run_configuration(
    cpu_qos,
    dma_qos,
    dnn_qos
):

    command = [
        SIMULATOR,
        "--dse",

        str(cpu_qos),
        str(dma_qos),
        str(dnn_qos),

        str(CPU_OUTSTANDING),
        str(DMA_OUTSTANDING),
        str(DNN_OUTSTANDING),
        str(AXI_QUEUE_DEPTH)
    ]

    print(
        "\n"
        + "=" * 70
    )

    print(
        "[DSE] Running configuration"
    )

    print(
        f"      CPU QoS = {cpu_qos}"
    )

    print(
        f"      DMA QoS = {dma_qos}"
    )

    print(
        f"      DNN QoS = {dnn_qos}"
    )

    print(
        "=" * 70
    )

    try:

        result = subprocess.run(
            command,
            cwd=PROJECT_ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            check=False
        )

    except Exception as exc:

        print(
            f"[ERROR] Could not execute simulator: {exc}"
        )

        return None

    if result.returncode != 0:

        print(
            "[ERROR] Simulator failed."
        )

        print(
            result.stdout
        )

        return None

    parsed = parse_result(
        result.stdout
    )

    if parsed is None:

        print(
            "[ERROR] PHASE18_RESULT line not found."
        )

        print(
            result.stdout
        )

        return None

    print(
        "[DSE] Result:"
    )

    print(
        f"      CPU P99     = "
        f"{parsed['cpu_p99_ns']:.2f} ns"
    )

    print(
        f"      Accelerator = "
        f"{parsed['accelerator_throughput_gbps']:.2f} GB/s"
    )

    return parsed


# ============================================================
# Pareto frontier
#
# Objective 1:
#     minimize CPU P99 latency
#
# Objective 2:
#     maximize accelerator throughput
#
# A point is dominated if another configuration has:
#
#     CPU P99 <= this point
#     AND
#     accelerator throughput >= this point
#
# with at least one strict improvement.
# ============================================================

def compute_pareto_frontier(results):

    pareto = []

    for candidate in results:

        candidate_latency = (
            candidate["cpu_p99_ns"]
        )

        candidate_throughput = (
            candidate[
                "accelerator_throughput_gbps"
            ]
        )

        dominated = False

        for other in results:

            if other is candidate:
                continue

            other_latency = (
                other["cpu_p99_ns"]
            )

            other_throughput = (
                other[
                    "accelerator_throughput_gbps"
                ]
            )

            no_worse = (
                other_latency <= candidate_latency
                and
                other_throughput >= candidate_throughput
            )

            strictly_better = (
                other_latency < candidate_latency
                or
                other_throughput > candidate_throughput
            )

            if no_worse and strictly_better:

                dominated = True
                break

        if not dominated:

            pareto.append(candidate)

    pareto.sort(
        key=lambda x: (
            x["cpu_p99_ns"],
            -x["accelerator_throughput_gbps"]
        )
    )

    return pareto


# ============================================================
# Write DSE CSV
# ============================================================

def write_dse_csv(results):

    fieldnames = [
        "policy",
        "cpu_qos",
        "dma_qos",
        "dnn_qos",
        "cpu_outstanding",
        "dma_outstanding",
        "dnn_outstanding",
        "axi_queue_depth",
        "cpu_avg_ns",
        "cpu_p50_ns",
        "cpu_p95_ns",
        "cpu_p99_ns",
        "cpu_max_ns",
        "accelerator_throughput_gbps"
    ]

    with open(
        DSE_CSV,
        "w",
        newline=""
    ) as file:

        writer = csv.DictWriter(
            file,
            fieldnames=fieldnames
        )

        writer.writeheader()

        for result in results:

            writer.writerow(
                {
                    field: result[field]
                    for field in fieldnames
                }
            )


# ============================================================
# Write Pareto CSV
# ============================================================

def write_pareto_csv(pareto):

    fieldnames = [
        "policy",
        "cpu_qos",
        "dma_qos",
        "dnn_qos",
        "cpu_outstanding",
        "dma_outstanding",
        "dnn_outstanding",
        "axi_queue_depth",
        "cpu_avg_ns",
        "cpu_p50_ns",
        "cpu_p95_ns",
        "cpu_p99_ns",
        "cpu_max_ns",
        "accelerator_throughput_gbps"
    ]

    with open(
        PARETO_CSV,
        "w",
        newline=""
    ) as file:

        writer = csv.DictWriter(
            file,
            fieldnames=fieldnames
        )

        writer.writeheader()

        for result in pareto:

            writer.writerow(
                {
                    field: result[field]
                    for field in fieldnames
                }
            )


# ============================================================
# Plot Pareto frontier
# ============================================================

def write_pareto_plot(
    results,
    pareto
):

    try:

        import matplotlib.pyplot as plt

    except ImportError:

        print(
            "\n[WARNING] matplotlib is not installed."
        )

        print(
            "[WARNING] CSV files were still generated."
        )

        return

    x_all = [
        result["cpu_p99_ns"]
        for result in results
    ]

    y_all = [
        result[
            "accelerator_throughput_gbps"
        ]
        for result in results
    ]

    x_pareto = [
        result["cpu_p99_ns"]
        for result in pareto
    ]

    y_pareto = [
        result[
            "accelerator_throughput_gbps"
        ]
        for result in pareto
    ]

    plt.figure(
        figsize=(9, 6)
    )

    plt.scatter(
        x_all,
        y_all,
        label="DSE configurations"
    )

    plt.plot(
        x_pareto,
        y_pareto,
        marker="o",
        label="Pareto frontier"
    )

    plt.xlabel(
        "CPU P99 Latency (ns)"
    )

    plt.ylabel(
        "Accelerator Throughput (GB/s)"
    )

    plt.title(
        "SoC Performance Model: CPU Tail Latency vs Accelerator Throughput"
    )

    plt.grid(
        True,
        alpha=0.3
    )

    plt.legend()

    plt.tight_layout()

    plt.savefig(
        PARETO_PNG,
        dpi=200
    )

    plt.close()


# ============================================================
# Main
# ============================================================

def main():

    if not os.path.exists(SIMULATOR):

        print(
            f"[ERROR] Simulator not found:"
        )

        print(
            f"        {SIMULATOR}"
        )

        print(
            "\nBuild the project first with:"
        )

        print(
            "cd ~/axi-soc-performance-model/build"
        )

        print(
            "make -j$(nproc)"
        )

        sys.exit(1)

    os.makedirs(
        RESULTS_DIR,
        exist_ok=True
    )

    configurations = list(
        itertools.product(
            CPU_QOS_VALUES,
            DMA_QOS_VALUES,
            DNN_QOS_VALUES
        )
    )

    total = len(
        configurations
    )

    print(
        "\n"
        + "=" * 70
    )

    print(
        " PHASE 19: AUTOMATED DESIGN-SPACE EXPLORATION"
    )

    print(
        "=" * 70
    )

    print(
        f"Simulator : {SIMULATOR}"
    )

    print(
        f"Configurations : {total}"
    )

    print(
        ""
    )

    print(
        "CPU QoS values : "
        + str(CPU_QOS_VALUES)
    )

    print(
        "DMA QoS values : "
        + str(DMA_QOS_VALUES)
    )

    print(
        "DNN QoS values : "
        + str(DNN_QOS_VALUES)
    )

    print(
        ""
    )

    print(
        "Fixed:"
    )

    print(
        f"CPU outstanding = {CPU_OUTSTANDING}"
    )

    print(
        f"DMA outstanding = {DMA_OUTSTANDING}"
    )

    print(
        f"DNN outstanding = {DNN_OUTSTANDING}"
    )

    print(
        f"AXI queue depth = {AXI_QUEUE_DEPTH}"
    )

    print(
        "=" * 70
    )

    results = []

    for index, configuration in enumerate(
        configurations,
        start=1
    ):

        cpu_qos, dma_qos, dnn_qos = (
            configuration
        )

        print(
            f"\n[DSE] Configuration "
            f"{index}/{total}"
        )

        result = run_configuration(
            cpu_qos,
            dma_qos,
            dnn_qos
        )

        if result is None:

            print(
                "[ERROR] Configuration failed."
            )

            print(
                "[ERROR] Stopping DSE to avoid "
                "an incomplete dataset."
            )

            sys.exit(1)

        results.append(
            result
        )

    # --------------------------------------------------------
    # Save complete DSE dataset
    # --------------------------------------------------------

    write_dse_csv(
        results
    )

    # --------------------------------------------------------
    # Compute Pareto frontier
    # --------------------------------------------------------

    pareto = compute_pareto_frontier(
        results
    )

    write_pareto_csv(
        pareto
    )

    write_pareto_plot(
        results,
        pareto
    )

    # --------------------------------------------------------
    # Final summary
    # --------------------------------------------------------

    print(
        "\n"
        + "=" * 70
    )

    print(
        " PHASE 19 COMPLETE"
    )

    print(
        "=" * 70
    )

    print(
        f"Configurations evaluated : "
        f"{len(results)}"
    )

    print(
        f"Pareto configurations     : "
        f"{len(pareto)}"
    )

    print(
        ""
    )

    print(
        "Pareto frontier:"
    )

    print(
        "-" * 70
    )

    for index, result in enumerate(
        pareto,
        start=1
    ):

        print(
            f"{index:2d}. "
            f"CPU QoS={result['cpu_qos']:2d} "
            f"DMA QoS={result['dma_qos']:2d} "
            f"DNN QoS={result['dnn_qos']:2d} | "
            f"P99={result['cpu_p99_ns']:8.2f} ns | "
            f"Accel={result['accelerator_throughput_gbps']:6.2f} GB/s"
        )

    print(
        "-" * 70
    )

    print(
        f"\nDSE results:"
    )

    print(
        f"  {DSE_CSV}"
    )

    print(
        f"\nPareto results:"
    )

    print(
        f"  {PARETO_CSV}"
    )

    print(
        f"\nPareto plot:"
    )

    print(
        f"  {PARETO_PNG}"
    )

    print(
        "\nThe dataset uses:"
    )

    print(
        "  Objective 1 = minimize CPU P99 latency"
    )

    print(
        "  Objective 2 = maximize accelerator throughput"
    )

    print(
        "=" * 70
    )


if __name__ == "__main__":
    main()
