#include "cpu_traffic_generator.h"

#include <iostream>


tlm::tlm_sync_enum CpuTrafficGenerator::nb_transport_bw(
    tlm::tlm_generic_payload& trans,
    tlm::tlm_phase& phase,
    sc_time& delay)
{
    AXIExtension* axi =
        trans.get_extension<AXIExtension>();

    if (axi == nullptr) {
        return tlm::TLM_ACCEPTED;
    }

    for (unsigned int i = 0; i < max_mshr; i++) {

        if (mshr_table[i].valid &&
            mshr_table[i].request_id == axi->parent_id) {

            /*
             * One child response has completed.
             */
            mshr_table[i].pending_children--;

            /*
             * The logical CPU request is complete only
             * when all of its AXI children have returned.
             */
            if (mshr_table[i].pending_children == 0) {

                mshr_table[i].valid = false;

                sc_time latency =
                    sc_time_stamp() -
                    mshr_table[i].issue_time;

                double latency_ns =
                    latency.to_seconds() * 1.0e9;

                /*
                 * Phase 17:
                 * Record parent-level CPU latency.
                 */
                completed_latencies_ns.push_back(
                    latency_ns
                );

                completed_parent_requests++;

                last_completion_time =
                    sc_time_stamp();

                has_completed = true;

                std::cout
                    << "[CPU] MSHR "
                    << i
                    << " FREED! Parent "
                    << axi->parent_id
                    << " fully complete. Latency: "
                    << latency
                    << std::endl;

                mshr_freed_event.notify();
            }

            break;
        }
    }

    /*
     * The CPU owns the transaction after the response
     * has returned.
     */
    trans.clear_extension<AXIExtension>();

    delete axi;
    delete &trans;

    return tlm::TLM_COMPLETED;
}


void CpuTrafficGenerator::dispatch_payload(
    uint64_t start_addr,
    unsigned int total_length,
    unsigned int parent_id,
    unsigned int child_id,
    int mshr_idx)
{
    /*
     * AXI 4-KB boundary handling.
     */
    uint64_t next_boundary =
        ((start_addr / 4096) + 1) * 4096;

    uint64_t end_addr =
        start_addr + total_length;

    if (end_addr > next_boundary) {

        unsigned int chunk1_length =
            static_cast<unsigned int>(
                next_boundary - start_addr
            );

        unsigned int chunk2_length =
            total_length - chunk1_length;

        dispatch_payload(
            start_addr,
            chunk1_length,
            parent_id,
            1,
            mshr_idx
        );

        dispatch_payload(
            next_boundary,
            chunk2_length,
            parent_id,
            2,
            mshr_idx
        );

        return;
    }

    mshr_table[mshr_idx].pending_children++;

    auto* payload =
        new tlm::tlm_generic_payload();

    payload->set_command(
        tlm::TLM_WRITE_COMMAND
    );

    payload->set_address(
        start_addr
    );

    payload->set_data_length(
        total_length
    );

    payload->set_response_status(
        tlm::TLM_INCOMPLETE_RESPONSE
    );

    AXIExtension* axi =
        new AXIExtension();

    axi->master_id = 0;
    axi->qos = qos;
    axi->parent_id = parent_id;
    axi->child_id = child_id;

    payload->set_extension(axi);

    tlm::tlm_phase phase =
        tlm::BEGIN_REQ;

    sc_time delay =
        SC_ZERO_TIME;

    std::cout
        << "[CPU] Firing Child "
        << child_id
        << " of Parent "
        << parent_id
        << " at "
        << sc_time_stamp()
        << std::endl;

    socket->nb_transport_fw(
        *payload,
        phase,
        delay
    );
}


void CpuTrafficGenerator::send_logical_request(
    uint64_t start_addr,
    unsigned int total_length)
{
    int free_idx = -1;

    /*
     * Closed-loop CPU MSHR behavior.
     */
    while (true) {

        for (unsigned int i = 0;
             i < max_mshr;
             i++) {

            if (!mshr_table[i].valid) {
                free_idx =
                    static_cast<int>(i);
                break;
            }
        }

        if (free_idx != -1) {
            break;
        }

        std::cout
            << "[CPU] STALL! All "
            << max_mshr
            << " MSHRs full at "
            << sc_time_stamp()
            << ". Waiting..."
            << std::endl;

        wait(mshr_freed_event);
    }

    unsigned int pid =
        transaction_counter++;

    mshr_table[free_idx].valid = true;

    mshr_table[free_idx].request_id =
        pid;

    mshr_table[free_idx].pending_children =
        0;

    mshr_table[free_idx].issue_time =
        sc_time_stamp();

    if (!has_issued) {
        first_issue_time =
            sc_time_stamp();

        has_issued = true;
    }

    std::cout
        << "[CPU] Allocated MSHR "
        << free_idx
        << " for Parent "
        << pid
        << std::endl;

    dispatch_payload(
        start_addr,
        total_length,
        pid,
        0,
        free_idx
    );
}


void CpuTrafficGenerator::generate_traffic()
{
    uint64_t test_address =
        0x0FE0;

    for (int i = 0; i < 10; i++) {

        send_logical_request(
            test_address,
            64
        );

        test_address += 128;

        wait(
            10,
            SC_NS
        );
    }
}
