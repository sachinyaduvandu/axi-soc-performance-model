#ifndef AXI_INTERCONNECT_H
#define AXI_INTERCONNECT_H

#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/simple_target_socket.h>
#include <tlm_utils/simple_initiator_socket.h>
#include "axi_extension.h"
#include "../arbitration/arbitration_policy.h"
#include <queue>
#include <array>

SC_MODULE(AxiInterconnect) {
    static const int NUM_MASTERS = 3;

    tlm_utils::simple_target_socket<AxiInterconnect> cpu_socket;
    tlm_utils::simple_target_socket<AxiInterconnect> dma_socket;
    tlm_utils::simple_target_socket<AxiInterconnect> dnn_socket;

    tlm_utils::simple_initiator_socket<AxiInterconnect> mem_socket;

    sc_core::sc_event request_arrived_event;
    std::array<sc_core::sc_event, NUM_MASTERS> space_freed_event;
    std::array<std::queue<tlm::tlm_generic_payload*>, NUM_MASTERS> request_queue;

    unsigned int max_queue_depth;

    ArbitrationPolicy* policy;

    SC_HAS_PROCESS(AxiInterconnect);

    AxiInterconnect(sc_core::sc_module_name name, unsigned int queue_depth = 16)
        : sc_module(name),
          cpu_socket("cpu_socket"),
          dma_socket("dma_socket"),
          dnn_socket("dnn_socket"),
          mem_socket("mem_socket"),
          max_queue_depth(queue_depth),
          policy(nullptr) {
        cpu_socket.register_nb_transport_fw(this, &AxiInterconnect::nb_transport_fw_cpu);
        dma_socket.register_nb_transport_fw(this, &AxiInterconnect::nb_transport_fw_dma);
        dnn_socket.register_nb_transport_fw(this, &AxiInterconnect::nb_transport_fw_dnn);

        mem_socket.register_nb_transport_bw(this, &AxiInterconnect::nb_transport_bw_mem);

        SC_THREAD(arbitrate_loop);
    }

    ~AxiInterconnect() {
        delete policy;
    }

    void set_policy(ArbitrationPolicy* p) {
        delete policy;
        policy = p;
    }

    tlm::tlm_sync_enum nb_transport_fw_cpu(tlm::tlm_generic_payload& trans,
                                            tlm::tlm_phase& phase, sc_time& delay);
    tlm::tlm_sync_enum nb_transport_fw_dma(tlm::tlm_generic_payload& trans,
                                            tlm::tlm_phase& phase, sc_time& delay);
    tlm::tlm_sync_enum nb_transport_fw_dnn(tlm::tlm_generic_payload& trans,
                                            tlm::tlm_phase& phase, sc_time& delay);

    tlm::tlm_sync_enum nb_transport_bw_mem(tlm::tlm_generic_payload& trans,
                                            tlm::tlm_phase& phase, sc_time& delay);

    void accept_request(int master_idx, tlm::tlm_generic_payload& trans);
    void arbitrate_loop();
};

#endif
