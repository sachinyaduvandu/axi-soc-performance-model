#ifndef QOS_PRIORITY_H
#define QOS_PRIORITY_H

#include "arbitration_policy.h"

class QosPriorityPolicy : public ArbitrationPolicy {
public:
    int select_request(const std::vector<bool>& has_pending,
                        const std::vector<unsigned int>& qos) override {
        int best = -1;
        unsigned int best_qos = 0;

        for (size_t i = 0; i < has_pending.size(); i++) {
            if (has_pending[i] && (best == -1 || qos[i] > best_qos)) {
                best = static_cast<int>(i);
                best_qos = qos[i];
            }
        }
        return best;
    }
};

#endif
