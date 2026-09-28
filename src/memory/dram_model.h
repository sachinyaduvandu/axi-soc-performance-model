#ifndef DRAM_MODEL_H
#define DRAM_MODEL_H

#include <systemc.h>
#include <vector>
#include <cstdint>
#include <limits>

class DramModel {
public:

    DramModel(unsigned int num_banks,
              sc_core::sc_time row_hit_latency,
              sc_core::sc_time row_miss_latency)
        : open_row_(num_banks, NO_OPEN_ROW),
          row_hit_latency_(row_hit_latency),
          row_miss_latency_(row_miss_latency) {}

    struct AccessResult {
        bool row_hit;
        sc_core::sc_time latency;
    };

    AccessResult access(unsigned int bank,
                        uint64_t row) {

        AccessResult result;

        result.row_hit =
            (open_row_[bank] == row);

        result.latency =
            result.row_hit
                ? row_hit_latency_
                : row_miss_latency_;

        open_row_[bank] = row;

        return result;
    }

    /*
     * Reset the currently open row in every bank.
     *
     * This allows independent experiment points to start
     * from the same DRAM state.
     */
    void reset() {

        std::fill(
            open_row_.begin(),
            open_row_.end(),
            NO_OPEN_ROW
        );
    }

private:

    static constexpr uint64_t NO_OPEN_ROW =
        std::numeric_limits<uint64_t>::max();

    std::vector<uint64_t> open_row_;

    sc_core::sc_time row_hit_latency_;
    sc_core::sc_time row_miss_latency_;
};

#endif
