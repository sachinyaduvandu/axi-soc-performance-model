import csv
import matplotlib.pyplot as plt

CSV_FILE = "results/memory_bandwidth.csv"

offered = []
achieved = []
latency = []

with open(CSV_FILE, newline="") as f:
    reader = csv.DictReader(f)

    for row in reader:
        offered.append(float(row["offered_bandwidth_gbps"]))
        achieved.append(float(row["achieved_bandwidth_gbps"]))
        latency.append(float(row["average_latency_ns"]))

# ------------------------------------------------------------
# Memory bandwidth saturation
# ------------------------------------------------------------

plt.figure(figsize=(8, 5))

plt.plot(
    offered,
    achieved,
    marker="o",
    label="Achieved bandwidth"
)

plt.plot(
    offered,
    offered,
    linestyle="--",
    label="Offered bandwidth"
)

plt.xlabel("Offered Bandwidth (GB/s)")
plt.ylabel("Bandwidth (GB/s)")
plt.title("Memory Bandwidth Saturation")
plt.grid(True, alpha=0.3)
plt.legend()
plt.tight_layout()

plt.savefig(
    "results/memory_bandwidth.png",
    dpi=200,
    bbox_inches="tight"
)

plt.close()

# ------------------------------------------------------------
# Memory latency under increasing load
# ------------------------------------------------------------

plt.figure(figsize=(8, 5))

plt.plot(
    offered,
    latency,
    marker="o"
)

plt.xlabel("Offered Bandwidth (GB/s)")
plt.ylabel("Average Transaction Latency (ns)")
plt.title("Memory Latency Under Increasing Offered Load")
plt.grid(True, alpha=0.3)
plt.tight_layout()

plt.savefig(
    "results/memory_latency.png",
    dpi=200,
    bbox_inches="tight"
)

plt.close()

print("Generated:")
print("  results/memory_bandwidth.png")
print("  results/memory_latency.png")
