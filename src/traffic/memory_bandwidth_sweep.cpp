#include "memory_bandwidth_sweep.h"

#include <iostream>
#include <iomanip>


/*
 * ============================================================
 * Destructor
 * ============================================================
 */

MemoryBandwidthSweep::~MemoryBandwidthSweep()
{
    /*
     * Transactions that have already completed are
     * deleted here.
     *
     * The corresponding data buffers are also released.
     */

    for (
        auto* trans :
        active_transactions
    ) {

        if (trans != nullptr) {

            AXIExtension* ext =
                nullptr;


            trans->get_extension(
                ext
            );


            if (ext != nullptr) {

                trans->clear_extension(
                    ext
                );

                delete ext;
            }


            delete trans;
        }
    }


    for (
        auto* ptr :
        active_data
    ) {

        delete[] ptr;
    }


    if (csv.is_open()) {

        csv.close();
    }


    if (stats_csv.is_open()) {

        stats_csv.close();
    }
}


/*
 * ============================================================
 * Send one memory transaction
 * ============================================================
 */

void MemoryBandwidthSweep::send_transaction(
    unsigned int transaction_id,
    unsigned int interval_ns)
{
    auto* trans =
        new tlm::tlm_generic_payload;


    auto* data_ptr =
        new unsigned char[
            TRANSACTION_SIZE
        ];


    /*
     * Initialize payload.
     */

    for (
        unsigned int i = 0;
        i < TRANSACTION_SIZE;
        ++i
    ) {

        data_ptr[i] = 0;
    }


    /*
     * Phase 15 uses writes.
     */

    trans->set_command(
        tlm::TLM_WRITE_COMMAND
    );


    /*
     * --------------------------------------------------------
     * Address mapping
     * --------------------------------------------------------
     *
     * Column bits = [7:0]
     * Bank bits   = [10:8]
     *
     * Incrementing by 0x100 therefore changes the bank
     * while keeping the column position unchanged.
     *
     * The resulting stream distributes requests across
     * the 8-bank DRAM model.
     */

    uint64_t address =
        0x10000ULL
        +
        static_cast<uint64_t>(
            transaction_id
        )
        *
        0x100ULL;


    trans->set_address(
        address
    );


    trans->set_data_ptr(
        data_ptr
    );


    trans->set_data_length(
        TRANSACTION_SIZE
    );


    trans->set_streaming_width(
        TRANSACTION_SIZE
    );


    trans->set_byte_enable_ptr(
        nullptr
    );


    trans->set_byte_enable_length(
        0
    );


    trans->set_dmi_allowed(
        false
    );


    trans->set_response_status(
        tlm::TLM_INCOMPLETE_RESPONSE
    );


    /*
     * --------------------------------------------------------
     * AXI extension
     * --------------------------------------------------------
     *
     * parent_id identifies the sweep transaction.
     */

    AXIExtension* ext =
        new AXIExtension;


    ext->master_id =
        99;

    ext->qos =
        1;

    ext->parent_id =
        transaction_id;

    ext->child_id =
        0;


    trans->set_extension(
        ext
    );


    /*
     * Keep ownership information.
     */

    active_transactions.push_back(
        trans
    );


    active_data.push_back(
        data_ptr
    );


    /*
     * Record exact injection time.
     */

    transaction_info.push_back({
        sc_time_stamp()
    });


    /*
     * --------------------------------------------------------
     * TLM non-blocking forward path
     * --------------------------------------------------------
     */

    tlm::tlm_phase phase =
        tlm::BEGIN_REQ;


    sc_time delay =
        SC_ZERO_TIME;


    socket->nb_transport_fw(
        *trans,
        phase,
        delay
    );
}


/*
 * ============================================================
 * TLM backward path
 * ============================================================
 */

tlm::tlm_sync_enum
MemoryBandwidthSweep::nb_transport_bw(
    tlm::tlm_generic_payload& trans,
    tlm::tlm_phase& phase,
    sc_time& delay)
{
    /*
     * A completed memory transaction returns through
     * BEGIN_RESP.
     */

    if (
        phase ==
        tlm::BEGIN_RESP
    ) {

        AXIExtension* ext =
            nullptr;


        trans.get_extension(
            ext
        );


        /*
         * parent_id identifies the sweep transaction.
         */

        if (
            ext != nullptr
            &&
            ext->parent_id <
                transaction_info.size()
        ) {

            sc_time tx_latency =
                sc_time_stamp()
                -
                transaction_info[
                    ext->parent_id
                ].start_time;


            latency_sum_ns +=
                tx_latency.to_seconds()
                *
                1.0e9;
        }


        /*
         * Count completed transaction.
         */

        completed_transactions++;


        last_completion_time =
            sc_time_stamp();


        /*
         * Wake the sweep process.
         */

        transaction_completed_event.notify();


        return tlm::TLM_COMPLETED;
    }


    return tlm::TLM_ACCEPTED;
}


/*
 * ============================================================
 * Sweep
 * ============================================================
 */

void MemoryBandwidthSweep::run_sweep()
{
    std::cout << "\n";

    std::cout
        << "============================================\n";

    std::cout
        << " PHASE 15: MEMORY BANDWIDTH SWEEP\n";

    std::cout
        << "============================================\n";

    std::cout
        << "Transaction size      : "
        << TRANSACTION_SIZE
        << " bytes\n";

    std::cout
        << "Transactions/point    : "
        << TRANSACTIONS_PER_POINT
        << "\n";

    std::cout
        << "DRAM banks             : 8\n";

    std::cout
        << "Row-hit latency        : 50 ns\n";

    std::cout
        << "Row-miss latency       : 120 ns\n";

    std::cout
        << "============================================\n\n";


    /*
     * Make sure the sweep has been connected to
     * the memory controller.
     */

    if (
        memory_controller == nullptr
    ) {

        std::cerr
            << "[BW-SWEEP] ERROR: "
            << "Memory controller was not connected."
            << std::endl;

        sc_stop();

        return;
    }


    /*
     * --------------------------------------------------------
     * Sweep offered traffic rate.
     * --------------------------------------------------------
     */

    for (
        unsigned int interval_ns :
        injection_intervals_ns
    ) {

        /*
         * ----------------------------------------------------
         * Reset Phase 15 metrics.
         * ----------------------------------------------------
         */

        completed_transactions =
            0;


        current_total_transactions =
            TRANSACTIONS_PER_POINT;


        latency_sum_ns =
            0.0;


        transaction_info.clear();


        sweep_start_time =
            sc_time_stamp();


        /*
         * ----------------------------------------------------
         * Reset Phase 16 memory statistics.
         * ----------------------------------------------------
         *
         * All previous transactions have completed and the
         * previous point has had its 500 ns idle period.
         */

        memory_controller->
            reset_statistics();


        /*
         * ----------------------------------------------------
         * Inject transactions.
         * ----------------------------------------------------
         *
         * First transaction is injected immediately.
         *
         * Subsequent transactions are separated by the
         * requested interval.
         */

        for (
            unsigned int i = 0;
            i < TRANSACTIONS_PER_POINT;
            ++i
        ) {

            send_transaction(
                i,
                interval_ns
            );


            if (
                i + 1 <
                TRANSACTIONS_PER_POINT
            ) {

                wait(
                    interval_ns,
                    SC_NS
                );
            }
        }


        /*
         * ----------------------------------------------------
         * Wait for all transactions to complete.
         * ----------------------------------------------------
         */

        while (
            completed_transactions
            <
            current_total_transactions
        ) {

            wait(
                transaction_completed_event
            );
        }


        /*
         * ----------------------------------------------------
         * Calculate completion time.
         * ----------------------------------------------------
         */

        double completion_time_ns =
            (
                last_completion_time
                -
                sweep_start_time
            ).to_seconds()
            *
            1.0e9;


        /*
         * ----------------------------------------------------
         * Offered bandwidth.
         * ----------------------------------------------------
         */

        double offered_bandwidth_gbps =
            static_cast<double>(
                TRANSACTION_SIZE
            )
            /
            static_cast<double>(
                interval_ns
            );


        /*
         * ----------------------------------------------------
         * Achieved bandwidth.
         * ----------------------------------------------------
         */

        double achieved_bandwidth_gbps =
            (
                static_cast<double>(
                    TRANSACTIONS_PER_POINT
                    *
                    TRANSACTION_SIZE
                )
                /
                completion_time_ns
            );


        /*
         * ----------------------------------------------------
         * Average transaction latency.
         * ----------------------------------------------------
         */

        double average_latency_ns =
            latency_sum_ns
            /
            static_cast<double>(
                TRANSACTIONS_PER_POINT
            );


        /*
         * ----------------------------------------------------
         * Phase 15 CSV
         * ----------------------------------------------------
         */

        csv
            << interval_ns
            << ","
            << std::fixed
            << std::setprecision(4)
            << offered_bandwidth_gbps
            << ","
            << completed_transactions
            << ","
            << TRANSACTION_SIZE
            << ","
            << completion_time_ns
            << ","
            << achieved_bandwidth_gbps
            << ","
            << average_latency_ns
            << "\n";


        csv.flush();


        /*
         * ----------------------------------------------------
         * Phase 16 statistics
         * ----------------------------------------------------
         */

        MemoryController::Statistics stats =
            memory_controller->
                get_statistics();


        double average_queue_wait_ns =
            memory_controller->
                get_average_queue_wait_ns();


        double row_hit_rate_percent =
            memory_controller->
                get_row_hit_rate_percent();


        double bank_utilization_percent =
            memory_controller->
                get_bank_utilization_percent(
                    completion_time_ns
                );


        uint64_t total_requests =
            stats.total_read_requests
            +
            stats.total_write_requests;


        uint64_t completed_requests =
            stats.completed_read_requests
            +
            stats.completed_write_requests;


        /*
         * Write Phase 16 CSV.
         */

        stats_csv
            << interval_ns
            << ","
            << std::fixed
            << std::setprecision(4)
            << offered_bandwidth_gbps
            << ","
            << completed_requests
            << ","
            << stats.total_read_requests
            << ","
            << stats.total_write_requests
            << ","
            << stats.completed_read_requests
            << ","
            << stats.completed_write_requests
            << ","
            << stats.row_hits
            << ","
            << stats.row_misses
            << ","
            << row_hit_rate_percent
            << ","
            << stats.max_read_queue_depth
            << ","
            << stats.max_write_queue_depth
            << ","
            << stats.max_total_queue_depth
            << ","
            << average_queue_wait_ns
            << ","
            << stats.total_service_time_ns
            << ","
            << bank_utilization_percent
            << "\n";


        stats_csv.flush();


        /*
         * ----------------------------------------------------
         * Console output
         * ----------------------------------------------------
         */

        std::cout
            << "[BW-SWEEP] Interval = "
            << interval_ns
            << " ns"
            << " | Offered = "
            << offered_bandwidth_gbps
            << " GB/s"
            << " | Achieved = "
            << achieved_bandwidth_gbps
            << " GB/s"
            << " | Avg Latency = "
            << average_latency_ns
            << " ns"
            << " | Completed = "
            << completed_transactions
            << "/"
            << current_total_transactions
            << std::endl;


        std::cout
            << "[MEM-STATS]"
            << " Requests = "
            << total_requests
            << " | Row Hits = "
            << stats.row_hits
            << " | Row Misses = "
            << stats.row_misses
            << " | Row Hit Rate = "
            << row_hit_rate_percent
            << "%"
            << " | Avg Queue Wait = "
            << average_queue_wait_ns
            << " ns"
            << " | Max Queue = "
            << stats.max_total_queue_depth
            << " | Bank Utilization = "
            << bank_utilization_percent
            << "%"
            << std::endl;


        /*
         * ----------------------------------------------------
         * Idle period between measurement points.
         * ----------------------------------------------------
         */

        wait(
            500,
            SC_NS
        );
    }


    /*
     * --------------------------------------------------------
     * Sweep complete.
     * --------------------------------------------------------
     */

    std::cout
        << "\n";


    std::cout
        << "[BW-SWEEP] Results written to "
        << "phase15_bandwidth.csv"
        << std::endl;


    std::cout
        << "[MEM-STATS] Results written to "
        << "phase16_memory_stats.csv"
        << std::endl;


    std::cout
        << "[BW-SWEEP] Phase 15/16 measurement complete."
        << std::endl;


    /*
     * End SystemC simulation.
     */

    sc_stop();
}
