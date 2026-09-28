#ifndef DNN_MODEL_H
#define DNN_MODEL_H

#include <systemc.h>
#include <tlm.h>
#include <tlm_utils/simple_initiator_socket.h>

#include "../axi/axi_extension.h"

#include <vector>

struct DnnMSHREntry {
    bool valid;
    unsigned int request_id;
    unsigned int pending_children;
    sc_core::sc_time issue_time;
};

SC_MODULE(DnnModel) {

    tlm_utils::simple_initiator_socket<DnnModel> socket;

    unsigned int transaction_counter;

    unsigned int max_outstanding;

    std::vector<DnnMSHREntry>
        outstanding_table;

    sc_core::sc_event slot_freed_event;

    unsigned int qos;

    /*
     * Phase 17 statistics.
     */
    unsigned long long completed_bytes;

    unsigned int completed_parent_requests;

    sc_core::sc_time first_issue_time;
    sc_core::sc_time last_completion_time;

    bool has_issued;
    bool has_completed;

    SC_HAS_PROCESS(DnnModel);

    DnnModel(
        sc_core::sc_module_name name,
        unsigned int qos_value = 4,
        unsigned int max_outstanding_value = 32
    )
        : sc_module(name),
          socket("socket"),
          transaction_counter(0),
          max_outstanding(max_outstanding_value),
          outstanding_table(max_outstanding_value),
          qos(qos_value),
          completed_bytes(0),
          completed_parent_requests(0),
          first_issue_time(SC_ZERO_TIME),
          last_completion_time(SC_ZERO_TIME),
          has_issued(false),
          has_completed(false) {

        for (unsigned int i = 0;
             i < max_outstanding;
             i++) {

            outstanding_table[i].valid = false;
            outstanding_table[i].request_id = 0;
            outstanding_table[i].pending_children = 0;
            outstanding_table[i].issue_time =
                SC_ZERO_TIME;
        }

        socket.register_nb_transport_bw(
            this,
            &DnnModel::nb_transport_bw
        );

        SC_THREAD(generate_traffic);
    }

    void generate_traffic();

    void send_logical_request(
        uint64_t start_addr,
        unsigned int total_length,
        bool is_write
    );

    void dispatch_payload(
        uint64_t start_addr,
        unsigned int total_length,
        unsigned int parent_id,
        unsigned int child_id,
        int mshr_idx,
        bool is_write
    );

    tlm::tlm_sync_enum nb_transport_bw(
        tlm::tlm_generic_payload& trans,
        tlm::tlm_phase& phase,
        sc_time& delay
    );

    unsigned long long get_completed_bytes() const {
        return completed_bytes;
    }

    unsigned int get_completed_requests() const {
        return completed_parent_requests;
    }

    sc_core::sc_time get_first_issue_time() const {
        return first_issue_time;
    }

    sc_core::sc_time get_last_completion_time() const {
        return last_completion_time;
    }

    bool has_activity() const {
        return has_issued && has_completed;
    }
};

#endif
