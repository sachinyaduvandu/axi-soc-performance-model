#ifndef MEMORY_CONTROLLER_H
#define MEMORY_CONTROLLER_H

#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/simple_target_socket.h>

#include "address_mapper.h"
#include "dram_model.h"

#include <queue>
#include <vector>
#include <unordered_map>
#include <cstdint>


SC_MODULE(MemoryController) {

    tlm_utils::simple_target_socket<MemoryController> socket;

    AddressMapper mapper;
    DramModel dram;


    /*
     * ------------------------------------------------------------
     * Memory request queues
     * ------------------------------------------------------------
     */

    std::queue<tlm::tlm_generic_payload*> read_queue;
    std::queue<tlm::tlm_generic_payload*> write_queue;

    sc_core::sc_event request_arrived_event;


    /*
     * ------------------------------------------------------------
     * Per-bank state
     * ------------------------------------------------------------
     */

    unsigned int num_banks;

    std::vector<bool> bank_busy;

    std::vector<sc_core::sc_event> bank_free_event;

    /*
     * Simulation time at which each bank became busy.
     *
     * Used to calculate bank busy time.
     */
    std::vector<sc_core::sc_time> bank_busy_start_time;


    /*
     * ------------------------------------------------------------
     * Phase 14: Read/Write Scheduling
     * ------------------------------------------------------------
     *
     * Normal mode:
     *     Reads have priority.
     *
     * Write-drain mode:
     *     Writes have priority once the write queue reaches
     *     the high watermark.
     *
     * Hysteresis:
     *     Write-drain continues until the write queue falls
     *     to the low watermark.
     */

    unsigned int scheduler_queue_depth;

    unsigned int write_high_watermark;

    unsigned int write_low_watermark;

    bool write_drain_mode;


    /*
     * ------------------------------------------------------------
     * Phase 16: Memory-system instrumentation
     * ------------------------------------------------------------
     */

    uint64_t total_read_requests;

    uint64_t total_write_requests;

    uint64_t completed_read_requests;

    uint64_t completed_write_requests;


    /*
     * DRAM row locality.
     */

    uint64_t row_hits;

    uint64_t row_misses;


    /*
     * Queue-depth statistics.
     */

    uint64_t max_read_queue_depth;

    uint64_t max_write_queue_depth;

    uint64_t max_total_queue_depth;


    /*
     * Timing statistics.
     *
     * Queue wait:
     *     time from memory-controller acceptance until
     *     the request actually obtains its bank.
     *
     * Service time:
     *     DRAM service latency.
     */

    double total_queue_wait_ns;

    double total_service_time_ns;


    /*
     * Per-bank accumulated busy time.
     */

    std::vector<double> bank_busy_time_ns;


    /*
     * Timestamp at which each request entered the
     * memory-controller queue.
     *
     * The payload pointer is used as the transaction key.
     */

    std::unordered_map<
        tlm::tlm_generic_payload*,
        sc_core::sc_time
    > enqueue_time;


    /*
     * ------------------------------------------------------------
     * Statistics snapshot
     * ------------------------------------------------------------
     */

    struct Statistics {

        uint64_t total_read_requests;

        uint64_t total_write_requests;

        uint64_t completed_read_requests;

        uint64_t completed_write_requests;

        uint64_t row_hits;

        uint64_t row_misses;

        uint64_t max_read_queue_depth;

        uint64_t max_write_queue_depth;

        uint64_t max_total_queue_depth;

        double total_queue_wait_ns;

        double total_service_time_ns;

        std::vector<double> bank_busy_time_ns;
    };


    SC_HAS_PROCESS(MemoryController);


    /*
     * ------------------------------------------------------------
     * Constructor
     * ------------------------------------------------------------
     */

    MemoryController(
        sc_core::sc_module_name name,

        unsigned int column_bits = 8,

        unsigned int bank_bits = 3,

        unsigned int channel_bits = 0,

        sc_core::sc_time row_hit_latency =
            sc_core::sc_time(50, SC_NS),

        sc_core::sc_time row_miss_latency =
            sc_core::sc_time(120, SC_NS),

        unsigned int scheduler_queue_depth = 16
    )

        : sc_module(name),

          socket("socket"),

          mapper(
              column_bits,
              bank_bits,
              channel_bits
          ),

          dram(
              1u << bank_bits,
              row_hit_latency,
              row_miss_latency
          ),

          num_banks(
              1u << bank_bits
          ),

          bank_busy(
              num_banks,
              false
          ),

          bank_free_event(
              num_banks
          ),

          bank_busy_start_time(
              num_banks,
              SC_ZERO_TIME
          ),

          scheduler_queue_depth(
              scheduler_queue_depth
          ),

          write_high_watermark(
              (scheduler_queue_depth * 80 + 99) / 100
          ),

          write_low_watermark(
              scheduler_queue_depth / 2
          ),

          write_drain_mode(
              false
          ),

          total_read_requests(
              0
          ),

          total_write_requests(
              0
          ),

          completed_read_requests(
              0
          ),

          completed_write_requests(
              0
          ),

          row_hits(
              0
          ),

          row_misses(
              0
          ),

          max_read_queue_depth(
              0
          ),

          max_write_queue_depth(
              0
          ),

          max_total_queue_depth(
              0
          ),

          total_queue_wait_ns(
              0.0
          ),

          total_service_time_ns(
              0.0
          ),

          bank_busy_time_ns(
              num_banks,
              0.0
          ) {

        socket.register_nb_transport_fw(
            this,
            &MemoryController::nb_transport_fw
        );

        SC_THREAD(
            scheduler_loop
        );
    }


    /*
     * ------------------------------------------------------------
     * TLM interface
     * ------------------------------------------------------------
     */

    tlm::tlm_sync_enum nb_transport_fw(
        tlm::tlm_generic_payload& trans,
        tlm::tlm_phase& phase,
        sc_time& delay
    );


    /*
     * ------------------------------------------------------------
     * Internal processes
     * ------------------------------------------------------------
     */

    void scheduler_loop();

    void service_request(
        tlm::tlm_generic_payload* trans
    );


    /*
     * ------------------------------------------------------------
     * Phase 16 statistics interface
     * ------------------------------------------------------------
     */

    void reset_statistics();

    Statistics get_statistics() const;

    double get_average_queue_wait_ns() const;

    double get_row_hit_rate_percent() const;

    double get_bank_utilization_percent(
        double observation_time_ns
    ) const;
};


#endif
