#ifndef FIXED_PRIORITY_H
#define FIXED_PRIORITY_H

#include "arbitration_policy.h"

class FixedPriorityPolicy : public ArbitrationPolicy {
public:
    int select_request(const std::vector<bool>& has_pending,
                        const std::vector<unsigned int>& /*qos*/) override {
        for (size_t i = 0; i < has_pending.size(); i++) {
            if (has_pending[i]) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }
};

#endif
