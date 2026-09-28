#include "axi_interconnect.h"
#include <iostream>

tlm::tlm_sync_enum AxiInterconnect::nb_transport_fw_cpu(tlm::tlm_generic_payload& trans,
                                                         tlm::tlm_phase& phase,
                                                         sc_time& delay) {
    accept_request(0, trans);
    return tlm::TLM_ACCEPTED;
}

tlm::tlm_sync_enum AxiInterconnect::nb_transport_fw_dma(tlm::tlm_generic_payload& trans,
                                                         tlm::tlm_phase& phase,
                                                         sc_time& delay) {
    accept_request(1, trans);
    return tlm::TLM_ACCEPTED;
}

tlm::tlm_sync_enum AxiInterconnect::nb_transport_fw_dnn(tlm::tlm_generic_payload& trans,
                                                         tlm::tlm_phase& phase,
                                                         sc_time& delay) {
    accept_request(2, trans);
    return tlm::TLM_ACCEPTED;
}

void AxiInterconnect::accept_request(int master_idx, tlm::tlm_generic_payload& trans) {
    static const char* names[NUM_MASTERS] = {"CPU", "DMA", "DNN"};

    while (request_queue[master_idx].size() >= max_queue_depth) {
        std::cout << "[AXI-IC] BACKPRESSURE: " << names[master_idx]
                   << " queue full (" << max_queue_depth << ") at "
                   << sc_time_stamp() << ". Master stalled." << std::endl;
        wait(space_freed_event[master_idx]);
    }

    request_queue[master_idx].push(&trans);
    request_arrived_event.notify();
}

void AxiInterconnect::arbitrate_loop() {
    while (true) {
        std::vector<bool> has_pending(NUM_MASTERS, false);
        std::vector<unsigned int> qos(NUM_MASTERS, 0);
        bool any_pending = false;

        for (int i = 0; i < NUM_MASTERS; i++) {
            if (!request_queue[i].empty()) {
                has_pending[i] = true;
                any_pending = true;
                AXIExtension* ext = request_queue[i].front()->get_extension<AXIExtension>();
                qos[i] = ext ? ext->qos : 0;
            }
        }

        if (!any_pending) {
            wait(request_arrived_event);
            continue;
        }

        int winner = policy->select_request(has_pending, qos);
        if (winner < 0) {
            wait(request_arrived_event);
            continue;
        }

        tlm::tlm_generic_payload* trans = request_queue[winner].front();
        request_queue[winner].pop();

        space_freed_event[winner].notify();

        AXIExtension* ext = trans->get_extension<AXIExtension>();
        static const char* names[NUM_MASTERS] = {"CPU", "DMA", "DNN"};
        std::cout << "[AXI-IC] Granting " << names[winner]
                  << " (QoS " << (ext ? ext->qos : 0) << ") at "
                  << sc_time_stamp() << " | queue depth now "
                  << request_queue[winner].size() << "/" << max_queue_depth
                  << std::endl;

        tlm::tlm_phase phase = tlm::BEGIN_REQ;
        sc_time delay = SC_ZERO_TIME;
        mem_socket->nb_transport_fw(*trans, phase, delay);

        wait(1, SC_NS);
    }
}

tlm::tlm_sync_enum AxiInterconnect::nb_transport_bw_mem(tlm::tlm_generic_payload& trans,
                                                         tlm::tlm_phase& phase,
                                                         sc_time& delay) {
    AXIExtension* ext = trans.get_extension<AXIExtension>();
    unsigned int master_id = ext ? ext->master_id : 0;

    switch (master_id) {
        case 0: cpu_socket->nb_transport_bw(trans, phase, delay); break;
        case 1: dma_socket->nb_transport_bw(trans, phase, delay); break;
        case 2: dnn_socket->nb_transport_bw(trans, phase, delay); break;
        default:
            std::cout << "[AXI-IC] WARNING: response with unknown master_id "
                      << master_id << " dropped." << std::endl;
            break;
    }
    return tlm::TLM_COMPLETED;
}
