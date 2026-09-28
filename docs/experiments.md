# Experiments

## Phase 15 — Memory Bandwidth Sweep

The memory subsystem was evaluated using 256-byte transactions and
injection intervals of:

- 32 ns
- 16 ns
- 8 ns
- 4 ns
- 2 ns
- 1 ns

This corresponds to nominal offered bandwidths from 8 GB/s to 256 GB/s.

Measured results:

| Offered BW | Achieved BW | Average Latency |
|---:|---:|---:|
| 8 GB/s | 7.915 GB/s | 120 ns |
| 16 GB/s | 15.604 GB/s | 120 ns |
| 32 GB/s | 16.821 GB/s | 988 ns |
| 64 GB/s | 16.943 GB/s | 1484 ns |
| 128 GB/s | 17.005 GB/s | 1732 ns |
| 256 GB/s | 17.036 GB/s | 1856 ns |

The modeled memory subsystem reaches approximately 17 GB/s of sustained
throughput under this configuration. Increasing offered load beyond the
saturation region increases latency rather than providing proportional
bandwidth growth.

Results:

`results/phase15_bandwidth.csv`

---

## Phase 16 — Memory Statistics

Instrumentation was added to collect:

- Read/write request counts
- Completed transactions
- Row hits/misses
- Row-hit rate
- Queue depth
- Memory-system waiting time
- Service time
- Bank utilization

Results:

`results/phase16_memory_stats.csv`

---

## Phase 17/18 — End-to-End Performance

The complete CPU/DMA/DNN workload was measured using:

- CPU average latency
- P50 latency
- P95 latency
- P99 latency
- Maximum latency
- DMA completed bytes
- DNN completed bytes
- Accelerator traffic throughput
- Workload duration

The simulator also supports passing architecture parameters through the
command line for automated design-space exploration.

---

## Phase 19 — QoS Design-Space Exploration

A Python script evaluated combinations of:

- CPU QoS
- DMA QoS
- DNN QoS

while keeping the outstanding-transaction capacities and AXI queue
depth fixed.

Results:

`results/dse_results.csv`

The initial sweep showed that the workload was not sensitive to many
of the QoS combinations. This motivated a more targeted experiment
rather than treating duplicate objective points as distinct Pareto
solutions.

---

## Phase 20 — CPU Outstanding-Request Sweep

A focused DSE varied CPU outstanding-request capacity:

| CPU Outstanding | CPU P99 | Accelerator Throughput |
|---:|---:|---:|
| 2 | 1324.0 ns | 7.96 GB/s |
| 4 | 1354.6 ns | 9.96 GB/s |
| 8 | 1354.6 ns | 10.46 GB/s |
| 16 | 1354.6 ns | 10.46 GB/s |

The resulting Pareto frontier contains:

| CPU Outstanding | CPU P99 | Accelerator Throughput |
|---:|---:|---:|
| 2 | 1324.0 ns | 7.96 GB/s |
| 8 | 1354.6 ns | 10.46 GB/s |

Increasing CPU outstanding capacity from 2 to 8 increased modeled
accelerator traffic throughput by approximately 31.4%, while CPU P99
latency increased by approximately 2.3%.

Increasing capacity from 8 to 16 produced no additional throughput
benefit for this workload.

Results:

- `results/outstanding_dse.csv`
- `results/outstanding_pareto.csv`
- `results/outstanding_pareto.png`
