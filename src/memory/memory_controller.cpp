#include "memory_controller.h"

#include <iostream>


namespace {

double to_ns(const sc_time& time)
{
    return time.to_seconds() * 1.0e9;
}

}


/*
 * ============================================================
 * TLM forward path
 * ============================================================
 */

tlm::tlm_sync_enum
MemoryController::nb_transport_fw(
    tlm::tlm_generic_payload& trans,
    tlm::tlm_phase& phase,
    sc_time& delay)
{
    AddressMapper::MappedAddress m =
        mapper.map(
            trans.get_address()
        );

    bool is_write =
        (trans.get_command() ==
         tlm::TLM_WRITE_COMMAND);


    std::cout
        << "[MEM] "
        << (is_write ? "WRITE" : "READ ")
        << " request to 0x"
        << std::hex
        << trans.get_address()
        << std::dec
        << " -> Channel "
        << m.channel
        << " Bank "
        << m.bank
        << " Row "
        << m.row
        << " Col "
        << m.column
        << " at "
        << sc_time_stamp()
        << std::endl;


    /*
     * Record when the request entered the memory
     * controller.
     */
    enqueue_time[&trans] =
        sc_time_stamp();


    /*
     * Place request into the appropriate queue.
     */

    if (is_write) {

        write_queue.push(
            &trans
        );

        total_write_requests++;


        if (write_queue.size() >
            max_write_queue_depth) {

            max_write_queue_depth =
                write_queue.size();
        }

    } else {

        read_queue.push(
            &trans
        );

        total_read_requests++;


        if (read_queue.size() >
            max_read_queue_depth) {

            max_read_queue_depth =
                read_queue.size();
        }
    }


    /*
     * Track combined queue occupancy.
     */

    uint64_t total_depth =
        static_cast<uint64_t>(
            read_queue.size()
        )
        +
        static_cast<uint64_t>(
            write_queue.size()
        );


    if (total_depth >
        max_total_queue_depth) {

        max_total_queue_depth =
            total_depth;
    }


    request_arrived_event.notify();


    return tlm::TLM_ACCEPTED;
}


/*
 * ============================================================
 * Read/write scheduler
 * ============================================================
 */

void MemoryController::scheduler_loop()
{
    while (true) {

        /*
         * No pending work.
         */

        if (read_queue.empty() &&
            write_queue.empty()) {

            wait(
                request_arrived_event
            );

            continue;
        }


        /*
         * ----------------------------------------------------
         * Enter write-drain mode
         * ----------------------------------------------------
         */

        if (!write_drain_mode &&
            write_queue.size() >=
                write_high_watermark) {

            write_drain_mode = true;


            std::cout
                << "[MEM-SCHED] Entering WRITE-DRAIN mode"
                << " | writes="
                << write_queue.size()
                << " | reads="
                << read_queue.size()
                << " | high_watermark="
                << write_high_watermark
                << " at "
                << sc_time_stamp()
                << std::endl;
        }


        /*
         * ----------------------------------------------------
         * Exit write-drain mode
         * ----------------------------------------------------
         */

        if (write_drain_mode &&
            write_queue.size() <=
                write_low_watermark) {

            write_drain_mode = false;


            std::cout
                << "[MEM-SCHED] Exiting WRITE-DRAIN mode"
                << " | writes="
                << write_queue.size()
                << " | reads="
                << read_queue.size()
                << " | low_watermark="
                << write_low_watermark
                << " at "
                << sc_time_stamp()
                << std::endl;
        }


        tlm::tlm_generic_payload* trans =
            nullptr;


        /*
         * ----------------------------------------------------
         * Select next request
         * ----------------------------------------------------
         */

        if (write_drain_mode) {

            /*
             * Write-drain mode:
             * writes have priority.
             */

            if (!write_queue.empty()) {

                trans =
                    write_queue.front();

                write_queue.pop();

            } else if (!read_queue.empty()) {

                trans =
                    read_queue.front();

                read_queue.pop();
            }

        } else {

            /*
             * Normal mode:
             * reads have priority.
             */

            if (!read_queue.empty()) {

                trans =
                    read_queue.front();

                read_queue.pop();

            } else if (!write_queue.empty()) {

                trans =
                    write_queue.front();

                write_queue.pop();
            }
        }


        /*
         * Launch the selected request.
         */

        if (trans != nullptr) {

            sc_spawn(
                sc_bind(
                    &MemoryController::service_request,
                    this,
                    trans
                )
            );
        }


        /*
         * One scheduler issue opportunity per ns.
         */

        wait(
            1,
            SC_NS
        );
    }
}


/*
 * ============================================================
 * DRAM service
 * ============================================================
 */

void MemoryController::service_request(
    tlm::tlm_generic_payload* trans)
{
    AddressMapper::MappedAddress m =
        mapper.map(
            trans->get_address()
        );


    /*
     * --------------------------------------------------------
     * Wait until the target bank becomes available.
     * --------------------------------------------------------
     */

    while (
        bank_busy[m.bank]
    ) {

        wait(
            bank_free_event[m.bank]
        );
    }


    /*
     * --------------------------------------------------------
     * Queue wait measurement
     * --------------------------------------------------------
     *
     * This includes:
     *
     *   - time spent waiting in the controller queue
     *   - scheduler delay
     *   - time waiting for the selected bank
     *
     * It ends when the request obtains the bank.
     * --------------------------------------------------------
     */

    auto enqueue_it =
        enqueue_time.find(
            trans
        );


    if (enqueue_it !=
        enqueue_time.end()) {

        total_queue_wait_ns +=
            to_ns(
                sc_time_stamp()
                -
                enqueue_it->second
            );

        enqueue_time.erase(
            enqueue_it
        );
    }


    /*
     * --------------------------------------------------------
     * Occupy bank
     * --------------------------------------------------------
     */

    bank_busy[m.bank] =
        true;

    bank_busy_start_time[m.bank] =
        sc_time_stamp();


    /*
     * --------------------------------------------------------
     * DRAM row access
     * --------------------------------------------------------
     */

    DramModel::AccessResult result =
        dram.access(
            m.bank,
            m.row
        );


    if (result.row_hit) {

        row_hits++;

    } else {

        row_misses++;
    }


    std::cout
        << "[DRAM] Bank "
        << m.bank
        << " Row "
        << m.row
        << " -> "
        << (
            result.row_hit
                ? "ROW HIT "
                : "ROW MISS "
        )
        << "("
        << result.latency
        << ") at "
        << sc_time_stamp()
        << std::endl;


    /*
     * --------------------------------------------------------
     * DRAM service latency
     * --------------------------------------------------------
     */

    wait(
        result.latency
    );


    total_service_time_ns +=
        to_ns(
            result.latency
        );


    /*
     * --------------------------------------------------------
     * Accumulate bank busy time
     * --------------------------------------------------------
     */

    bank_busy_time_ns[m.bank] +=
        to_ns(
            sc_time_stamp()
            -
            bank_busy_start_time[m.bank]
        );


    /*
     * --------------------------------------------------------
     * Release bank
     * --------------------------------------------------------
     */

    bank_busy[m.bank] =
        false;

    bank_free_event[m.bank].notify();


    /*
     * --------------------------------------------------------
     * Completion accounting
     * --------------------------------------------------------
     */

    if (
        trans->get_command()
        ==
        tlm::TLM_WRITE_COMMAND
    ) {

        completed_write_requests++;

    } else {

        completed_read_requests++;
    }


    /*
     * --------------------------------------------------------
     * Send TLM response
     * --------------------------------------------------------
     */

    trans->set_response_status(
        tlm::TLM_OK_RESPONSE
    );


    tlm::tlm_phase phase =
        tlm::BEGIN_RESP;

    sc_time delay =
        SC_ZERO_TIME;


    socket->nb_transport_bw(
        *trans,
        phase,
        delay
    );
}


/*
 * ============================================================
 * Reset statistics
 * ============================================================
 */

void MemoryController::reset_statistics()
{
    total_read_requests =
        0;

    total_write_requests =
        0;

    completed_read_requests =
        0;

    completed_write_requests =
        0;

    row_hits =
        0;

    row_misses =
        0;

    max_read_queue_depth =
        0;

    max_write_queue_depth =
        0;

    max_total_queue_depth =
        0;

    total_queue_wait_ns =
        0.0;

    total_service_time_ns =
        0.0;


    std::fill(
        bank_busy_time_ns.begin(),
        bank_busy_time_ns.end(),
        0.0
    );


    /*
     * At the end of every Phase 15 sweep point,
     * all transactions have completed before this
     * function is called.
     *
     * Therefore there should be no outstanding
     * enqueue timestamps here.
     */

    enqueue_time.clear();


    /*
     * Reset DRAM open-row state so every experiment
     * point starts from the same memory state.
     */

    dram.reset();
}


/*
 * ============================================================
 * Statistics snapshot
 * ============================================================
 */

MemoryController::Statistics
MemoryController::get_statistics() const
{
    Statistics stats;


    stats.total_read_requests =
        total_read_requests;

    stats.total_write_requests =
        total_write_requests;

    stats.completed_read_requests =
        completed_read_requests;

    stats.completed_write_requests =
        completed_write_requests;

    stats.row_hits =
        row_hits;

    stats.row_misses =
        row_misses;

    stats.max_read_queue_depth =
        max_read_queue_depth;

    stats.max_write_queue_depth =
        max_write_queue_depth;

    stats.max_total_queue_depth =
        max_total_queue_depth;

    stats.total_queue_wait_ns =
        total_queue_wait_ns;

    stats.total_service_time_ns =
        total_service_time_ns;

    stats.bank_busy_time_ns =
        bank_busy_time_ns;


    return stats;
}


/*
 * ============================================================
 * Average queue wait
 * ============================================================
 */

double
MemoryController::get_average_queue_wait_ns() const
{
    uint64_t completed =
        completed_read_requests
        +
        completed_write_requests;


    if (completed == 0) {

        return 0.0;
    }


    return
        total_queue_wait_ns
        /
        static_cast<double>(
            completed
        );
}


/*
 * ============================================================
 * Row-hit rate
 * ============================================================
 */

double
MemoryController::get_row_hit_rate_percent() const
{
    uint64_t accesses =
        row_hits
        +
        row_misses;


    if (accesses == 0) {

        return 0.0;
    }


    return
        100.0
        *
        static_cast<double>(
            row_hits
        )
        /
        static_cast<double>(
            accesses
        );
}


/*
 * ============================================================
 * Aggregate bank utilization
 * ============================================================
 *
 * utilization =
 *
 *   total accumulated bank busy time
 *   --------------------------------
 *   observation time × number of banks
 *
 * The result is expressed as a percentage.
 * ============================================================
 */

double
MemoryController::get_bank_utilization_percent(
    double observation_time_ns) const
{
    if (
        observation_time_ns <= 0.0
        ||
        num_banks == 0
    ) {

        return 0.0;
    }


    double total_busy_ns =
        0.0;


    for (
        double busy_ns :
        bank_busy_time_ns
    ) {

        total_busy_ns +=
            busy_ns;
    }


    double total_available_ns =
        observation_time_ns
        *
        static_cast<double>(
            num_banks
        );


    return
        100.0
        *
        total_busy_ns
        /
        total_available_ns;
}
