#ifndef MEMORY_BANDWIDTH_SWEEP_H
#define MEMORY_BANDWIDTH_SWEEP_H

#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/simple_initiator_socket.h>

#include <vector>
#include <fstream>

#include "../axi/axi_extension.h"
#include "../memory/memory_controller.h"


SC_MODULE(MemoryBandwidthSweep) {

    tlm_utils::simple_initiator_socket<
        MemoryBandwidthSweep
    > socket;


    /*
     * ------------------------------------------------------------
     * Phase 15 configuration
     * ------------------------------------------------------------
     */

    static constexpr unsigned int TRANSACTION_SIZE =
        256;

    static constexpr unsigned int TRANSACTIONS_PER_POINT =
        256;


    const std::vector<unsigned int>
        injection_intervals_ns = {
            32,
            16,
            8,
            4,
            2,
            1
        };


    /*
     * ------------------------------------------------------------
     * Transaction timing information
     * ------------------------------------------------------------
     */

    struct TransactionInfo {

        sc_time start_time;
    };


    std::vector<
        tlm::tlm_generic_payload*
    > active_transactions;


    std::vector<
        unsigned char*
    > active_data;


    std::vector<
        TransactionInfo
    > transaction_info;


    /*
     * ------------------------------------------------------------
     * Phase 15 counters
     * ------------------------------------------------------------
     */

    unsigned int completed_transactions;

    unsigned int current_total_transactions;


    sc_time sweep_start_time;

    sc_time last_completion_time;


    double latency_sum_ns;


    /*
     * ------------------------------------------------------------
     * Phase 15 CSV
     * ------------------------------------------------------------
     */

    std::ofstream csv;


    /*
     * ------------------------------------------------------------
     * Phase 16 CSV
     * ------------------------------------------------------------
     */

    std::ofstream stats_csv;


    /*
     * ------------------------------------------------------------
     * Completion event
     * ------------------------------------------------------------
     */

    sc_event transaction_completed_event;


    /*
     * ------------------------------------------------------------
     * Memory controller being measured
     * ------------------------------------------------------------
     */

    MemoryController*
        memory_controller;


    SC_HAS_PROCESS(
        MemoryBandwidthSweep
    );


    /*
     * ------------------------------------------------------------
     * Constructor
     * ------------------------------------------------------------
     */

    MemoryBandwidthSweep(
        sc_core::sc_module_name name
    )

        : sc_module(name),

          socket("socket"),

          completed_transactions(
              0
          ),

          current_total_transactions(
              0
          ),

          sweep_start_time(
              SC_ZERO_TIME
          ),

          last_completion_time(
              SC_ZERO_TIME
          ),

          latency_sum_ns(
              0.0
          ),

          memory_controller(
              nullptr
          ) {


        socket.register_nb_transport_bw(
            this,
            &MemoryBandwidthSweep::nb_transport_bw
        );


        /*
         * Phase 15 results.
         */

        csv.open(
            "phase15_bandwidth.csv"
        );


        csv
            << "injection_interval_ns,"
            << "offered_bandwidth_gbps,"
            << "completed_transactions,"
            << "transaction_bytes,"
            << "completion_time_ns,"
            << "achieved_bandwidth_gbps,"
            << "average_latency_ns\n";


        /*
         * Phase 16 memory statistics.
         */

        stats_csv.open(
            "phase16_memory_stats.csv"
        );


        stats_csv
            << "injection_interval_ns,"
            << "offered_bandwidth_gbps,"
            << "completed_transactions,"
            << "read_requests,"
            << "write_requests,"
            << "completed_reads,"
            << "completed_writes,"
            << "row_hits,"
            << "row_misses,"
            << "row_hit_rate_percent,"
            << "max_read_queue_depth,"
            << "max_write_queue_depth,"
            << "max_total_queue_depth,"
            << "average_queue_wait_ns,"
            << "total_service_time_ns,"
            << "bank_utilization_percent\n";


        SC_THREAD(
            run_sweep
        );
    }


    /*
     * ------------------------------------------------------------
     * Destructor
     * ------------------------------------------------------------
     */

    ~MemoryBandwidthSweep();


    /*
     * ------------------------------------------------------------
     * Public methods
     * ------------------------------------------------------------
     */

    void run_sweep();


    void send_transaction(
        unsigned int transaction_id,
        unsigned int interval_ns
    );


    tlm::tlm_sync_enum nb_transport_bw(
        tlm::tlm_generic_payload& trans,
        tlm::tlm_phase& phase,
        sc_time& delay
    );


    /*
     * Connect the sweep to the memory controller so
     * Phase 16 can collect controller-level statistics.
     */

    void set_memory_controller(
        MemoryController& controller
    ) {
        memory_controller =
            &controller;
    }
};


#endif
