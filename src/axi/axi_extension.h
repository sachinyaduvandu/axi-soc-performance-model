#ifndef AXI_EXTENSION_H
#define AXI_EXTENSION_H

#include <tlm.h>

struct AXIExtension : public tlm::tlm_extension<AXIExtension> {
    unsigned int master_id;
    unsigned int qos;
    
    // NEW: Phase 4 Parent/Child Tracking
    unsigned int parent_id;
    unsigned int child_id;

    virtual tlm_extension_base* clone() const {
        AXIExtension* ext = new AXIExtension;
        ext->master_id = this->master_id;
        ext->qos = this->qos;
        ext->parent_id = this->parent_id;
        ext->child_id = this->child_id;
        return ext;
    }

    virtual void copy_from(tlm_extension_base const &ext) {
        const AXIExtension& a_ext = static_cast<const AXIExtension&>(ext);
        master_id = a_ext.master_id;
        qos = a_ext.qos;
        parent_id = a_ext.parent_id;
        child_id = a_ext.child_id;
    }
};

#endif
