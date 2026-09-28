#ifndef ARBITRATION_POLICY_H
#define ARBITRATION_POLICY_H

#include <vector>

class ArbitrationPolicy {
public:
    virtual ~ArbitrationPolicy() {}
    virtual int select_request(const std::vector<bool>& has_pending,
                                const std::vector<unsigned int>& qos) = 0;
};

#endif
