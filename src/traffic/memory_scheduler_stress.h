#ifndef MEMORY_SCHEDULER_STRESS_H
#define MEMORY_SCHEDULER_STRESS_H

#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/simple_initiator_socket.h>
#include <vector>
#include "../axi/axi_extension.h"

SC_MODULE(MemorySchedulerStress) {
    tlm_utils::simple_initiator_socket<MemorySchedulerStress> socket;

    std::vector<tlm::tlm_generic_payload> transactions;
    std::vector<unsigned char> data;

    SC_HAS_PROCESS(MemorySchedulerStress);

    MemorySchedulerStress(sc_core::sc_module_name name)
        : sc_module(name),
          socket("socket"),
          transactions(24),
          data(24 * 256, 0) {

        socket.register_nb_transport_bw(
            this,
            &MemorySchedulerStress::nb_transport_bw
        );

        SC_THREAD(generate_stress);
    }

    void generate_stress();

    tlm::tlm_sync_enum nb_transport_bw(
        tlm::tlm_generic_payload& trans,
        tlm::tlm_phase& phase,
        sc_time& delay
    );
};

#endif
