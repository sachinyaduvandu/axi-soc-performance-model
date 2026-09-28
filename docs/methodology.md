# Methodology

## Simulation

The SoC is modeled using SystemC and TLM-2.0.

Traffic generators create memory transactions which traverse the
interconnect and memory subsystem.

The model records transaction timestamps and completion events to derive
latency and throughput metrics.

## Latency

CPU request latency is measured at the parent transaction level.

The distribution is summarized using:

- Average
- P50
- P95
- P99
- Maximum

P99 latency is used as the primary CPU tail-latency objective in the
design-space exploration.

## Throughput

Accelerator traffic throughput is calculated from completed DMA and DNN
traffic over the measured workload interval.

The reported value represents aggregate modeled memory traffic rather
than useful application throughput.

## Memory Bandwidth

The bandwidth sweep injects fixed-size memory transactions at controlled
intervals.

Both offered bandwidth and achieved bandwidth are recorded.

As the offered load approaches the modeled memory-system capacity,
queueing and bank contention cause transaction latency to increase.

## Design-Space Exploration

The Python scripts invoke the compiled SystemC simulator for each
architecture configuration.

The simulator emits a machine-readable result line:

`PHASE18_RESULT`

The Python script parses these measurements and stores them in CSV
format.

## Pareto Analysis

Two objectives are evaluated:

1. Minimize CPU P99 latency.
2. Maximize accelerator traffic throughput.

A configuration is Pareto-optimal when another tested configuration
cannot improve one objective without worsening the other.

The resulting Pareto points are exported as CSV and plotted for
inspection.

## Reproducibility

The simulator and DSE scripts are included in the repository so that
the reported experiments can be repeated after building the project.
