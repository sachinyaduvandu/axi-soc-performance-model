# Architecture

## System Overview

The model represents a heterogeneous SoC with three traffic-generating
masters:

- CPU
- DMA engine
- DNN accelerator

The masters communicate through an AXI4-style interconnect and share a
banked memory subsystem.

```text
CPU ─────┐
         │
DMA ─────┼──> AXI Interconnect ──> Memory Controller ──> DRAM Model
         │
DNN ─────┘

CPU

The CPU model uses a configurable outstanding-request limit representing
an MSHR-like capacity constraint.

CPU traffic is modeled as memory transactions with parent-level latency
measurement.

DMA

The DMA model generates memory traffic with a configurable number of
outstanding transactions.

DNN Accelerator

The DNN model generates accelerator memory traffic and exposes a
configurable outstanding-transaction capacity.

AXI Interconnect

The interconnect models:

Transaction routing
AXI-style transaction metadata
Request buffering
Backpressure
Outstanding transactions
QoS-based arbitration

The interconnect supports configurable queue depth and QoS priorities.

Memory Controller

The memory controller provides:

Separate read/write queues
Read-priority scheduling
Hysteretic write-drain scheduling
Bank serialization
Memory timing integration
DRAM Model

The DRAM model contains multiple banks and tracks the currently open row
per bank.

A request is classified as a row hit or row miss.

The current architectural timing assumptions are:

Row hit: 50 ns
Row miss: 120 ns
Number of banks: 8

The model is intended for architectural exploration and is not intended
to reproduce the complete command-level timing behavior of a specific
JEDEC DRAM device.
