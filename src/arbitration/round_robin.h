#ifndef ROUND_ROBIN_H
#define ROUND_ROBIN_H

#include "arbitration_policy.h"

class RoundRobinPolicy : public ArbitrationPolicy {
private:
    int last_served;

public:
    RoundRobinPolicy() : last_served(-1) {}

    int select_request(const std::vector<bool>& has_pending,
                        const std::vector<unsigned int>& qos) override {
        int n = static_cast<int>(has_pending.size());
        for (int step = 1; step <= n; step++) {
            int idx = (last_served + step) % n;
            if (has_pending[idx]) {
                last_served = idx;
                return idx;
            }
        }
        return -1;
    }
};

#endif
