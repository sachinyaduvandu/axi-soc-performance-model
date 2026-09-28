#ifndef CPU_TRAFFIC_GENERATOR_H
#define CPU_TRAFFIC_GENERATOR_H

#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/simple_initiator_socket.h>

#include "../axi/axi_extension.h"

#include <vector>
#include <limits>

struct MSHREntry {
    bool valid;
    unsigned int request_id;
    unsigned int pending_children;
    sc_core::sc_time issue_time;
};

SC_MODULE(CpuTrafficGenerator) {

    tlm_utils::simple_initiator_socket<CpuTrafficGenerator> socket;

    unsigned int transaction_counter;

    unsigned int max_mshr;
    std::vector<MSHREntry> mshr_table;

    sc_core::sc_event mshr_freed_event;

    unsigned int qos;

    /*
     * Phase 17 performance statistics.
     *
     * One latency sample is recorded for every completed
     * logical CPU request (parent transaction).
     */
    std::vector<double> completed_latencies_ns;

    unsigned int completed_parent_requests;

    sc_core::sc_time first_issue_time;
    sc_core::sc_time last_completion_time;

    bool has_issued;
    bool has_completed;

    SC_HAS_PROCESS(CpuTrafficGenerator);

    CpuTrafficGenerator(
        sc_core::sc_module_name name,
        unsigned int qos_value = 15,
        unsigned int max_mshr_value = 8
    )
        : sc_module(name),
          socket("socket"),
          transaction_counter(0),
          max_mshr(max_mshr_value),
          mshr_table(max_mshr_value),
          qos(qos_value),
          completed_parent_requests(0),
          first_issue_time(SC_ZERO_TIME),
          last_completion_time(SC_ZERO_TIME),
          has_issued(false),
          has_completed(false) {

        for (unsigned int i = 0; i < max_mshr; i++) {
            mshr_table[i].valid = false;
            mshr_table[i].request_id = 0;
            mshr_table[i].pending_children = 0;
            mshr_table[i].issue_time = SC_ZERO_TIME;
        }

        socket.register_nb_transport_bw(
            this,
            &CpuTrafficGenerator::nb_transport_bw
        );

        SC_THREAD(generate_traffic);
    }

    void generate_traffic();

    void send_logical_request(
        uint64_t start_addr,
        unsigned int total_length
    );

    void dispatch_payload(
        uint64_t start_addr,
        unsigned int total_length,
        unsigned int parent_id,
        unsigned int child_id,
        int mshr_idx
    );

    tlm::tlm_sync_enum nb_transport_bw(
        tlm::tlm_generic_payload& trans,
        tlm::tlm_phase& phase,
        sc_time& delay
    );

    const std::vector<double>& get_latency_samples() const {
        return completed_latencies_ns;
    }

    unsigned int get_completed_requests() const {
        return completed_parent_requests;
    }

    bool has_activity() const {
        return has_issued && has_completed;
    }

    sc_core::sc_time get_first_issue_time() const {
        return first_issue_time;
    }

    sc_core::sc_time get_last_completion_time() const {
        return last_completion_time;
    }
};

#endif
