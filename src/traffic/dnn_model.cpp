#include "dnn_model.h"

#include <iostream>
#include <string>


tlm::tlm_sync_enum DnnModel::nb_transport_bw(
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
     * Count data at completion.
     *
     * Each AXI child contributes its payload size.
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
                    << "[DNN] Slot "
                    << i
                    << " FREED! GEMM Parent "
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


void DnnModel::dispatch_payload(
    uint64_t start_addr,
    unsigned int total_length,
    unsigned int parent_id,
    unsigned int child_id,
    int mshr_idx,
    bool is_write)
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
            mshr_idx,
            is_write
        );

        dispatch_payload(
            next_boundary,
            chunk2_length,
            parent_id,
            2,
            mshr_idx,
            is_write
        );

        return;
    }

    outstanding_table[mshr_idx]
        .pending_children++;

    auto* payload =
        new tlm::tlm_generic_payload();

    payload->set_command(
        is_write
            ? tlm::TLM_WRITE_COMMAND
            : tlm::TLM_READ_COMMAND
    );

    payload->set_address(
        start_addr
    );

    payload->set_data_length(
        total_length
    );

    AXIExtension* axi =
        new AXIExtension();

    axi->master_id = 2;
    axi->qos = qos;
    axi->parent_id = parent_id;
    axi->child_id = child_id;

    payload->set_extension(axi);

    tlm::tlm_phase phase =
        tlm::BEGIN_REQ;

    sc_time delay =
        SC_ZERO_TIME;

    std::string command =
        is_write ? "WRITE" : "READ ";

    std::cout
        << "[DNN] Firing "
        << command
        << " Child "
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


void DnnModel::send_logical_request(
    uint64_t start_addr,
    unsigned int total_length,
    bool is_write)
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
            << "[DNN] STALL! All "
            << max_outstanding
            << " slots full at "
            << sc_time_stamp()
            << ". Waiting..."
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
        free_idx,
        is_write
    );
}


void DnnModel::generate_traffic()
{
    uint64_t weight_addr =
        0x4000;

    uint64_t act_addr =
        0x8000;

    uint64_t out_addr =
        0xC000;

    for (int i = 0; i < 12; i++) {

        send_logical_request(
            weight_addr,
            256,
            false
        );

        send_logical_request(
            act_addr,
            256,
            false
        );

        send_logical_request(
            out_addr,
            256,
            true
        );

        weight_addr += 256;
        act_addr += 256;
        out_addr += 256;

        wait(
            5,
            SC_NS
        );
    }
}
