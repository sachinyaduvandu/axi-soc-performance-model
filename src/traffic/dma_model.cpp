#include "dma_model.h"

#include <iostream>


tlm::tlm_sync_enum DmaModel::nb_transport_bw(
    tlm::tlm_generic_payload& trans,
    tlm::tlm_phase& phase,
    sc_time& delay)
{
    AXIExtension* axi =
        trans.get_extension<AXIExtension>();

    if (axi == nullptr) {
        return tlm::TLM_ACCEPTED;
    }

    /*
     * Count successfully completed data.
     *
     * This is done per AXI child transaction so that
     * 4-KB burst splitting is accounted for correctly.
     */
    completed_bytes +=
        trans.get_data_length();

    for (unsigned int i = 0;
         i < max_outstanding;
         i++) {

        if (outstanding_table[i].valid &&
            outstanding_table[i].request_id ==
                axi->parent_id) {

            outstanding_table[i].pending_children--;

            if (outstanding_table[i].pending_children == 0) {

                outstanding_table[i].valid = false;

                completed_parent_requests++;

                last_completion_time =
                    sc_time_stamp();

                has_completed = true;

                std::cout
                    << "[DMA] Slot "
                    << i
                    << " FREED! Stream Parent "
                    << axi->parent_id
                    << " complete."
                    << std::endl;

                slot_freed_event.notify();
            }

            break;
        }
    }

    trans.clear_extension<AXIExtension>();

    delete axi;
    delete &trans;

    return tlm::TLM_COMPLETED;
}


void DmaModel::dispatch_payload(
    uint64_t start_addr,
    unsigned int total_length,
    unsigned int parent_id,
    unsigned int child_id,
    int mshr_idx)
{
    /*
     * AXI 4-KB boundary splitting.
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

    outstanding_table[mshr_idx]
        .pending_children++;

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

    AXIExtension* axi =
        new AXIExtension();

    axi->master_id = 1;
    axi->qos = qos;
    axi->parent_id = parent_id;
    axi->child_id = child_id;

    payload->set_extension(axi);

    tlm::tlm_phase phase =
        tlm::BEGIN_REQ;

    sc_time delay =
        SC_ZERO_TIME;

    std::cout
        << "[DMA] Firing Child "
        << child_id
        << " of Parent "
        << parent_id
        << " to 0x"
        << std::hex
        << start_addr
        << std::dec
        << " | Size: "
        << total_length
        << " at "
        << sc_time_stamp()
        << std::endl;

    socket->nb_transport_fw(
        *payload,
        phase,
        delay
    );
}


void DmaModel::send_logical_request(
    uint64_t start_addr,
    unsigned int total_length)
{
    int free_idx = -1;

    while (true) {

        for (unsigned int i = 0;
             i < max_outstanding;
             i++) {

            if (!outstanding_table[i].valid) {
                free_idx =
                    static_cast<int>(i);
                break;
            }
        }

        if (free_idx != -1) {
            break;
        }

        std::cout
            << "[DMA] STALL! All "
            << max_outstanding
            << " slots full at "
            << sc_time_stamp()
            << ". Waiting for bandwidth..."
            << std::endl;

        wait(slot_freed_event);
    }

    unsigned int pid =
        transaction_counter++;

    outstanding_table[free_idx].valid =
        true;

    outstanding_table[free_idx].request_id =
        pid;

    outstanding_table[free_idx].pending_children =
        0;

    outstanding_table[free_idx].issue_time =
        sc_time_stamp();

    if (!has_issued) {

        first_issue_time =
            sc_time_stamp();

        has_issued = true;
    }

    dispatch_payload(
        start_addr,
        total_length,
        pid,
        0,
        free_idx
    );
}


void DmaModel::generate_traffic()
{
    uint64_t stream_address =
        0x1f00;

    for (int i = 0; i < 20; i++) {

        send_logical_request(
            stream_address,
            256
        );

        stream_address += 256;

        wait(
            5,
            SC_NS
        );
    }
}
