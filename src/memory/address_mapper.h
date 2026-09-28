#ifndef ADDRESS_MAPPER_H
#define ADDRESS_MAPPER_H

#include <cstdint>

class AddressMapper {
public:
    struct MappedAddress {
        unsigned int channel;
        unsigned int bank;
        uint64_t row;
        unsigned int column;
    };

    AddressMapper(unsigned int column_bits = 8,
                  unsigned int bank_bits = 3,
                  unsigned int channel_bits = 0)
        : column_bits_(column_bits), bank_bits_(bank_bits), channel_bits_(channel_bits) {}

    MappedAddress map(uint64_t address) const {
        MappedAddress m;
        uint64_t shifted = address;

        uint64_t column_mask = (column_bits_ >= 64) ? ~0ULL : ((1ULL << column_bits_) - 1);
        m.column = static_cast<unsigned int>(shifted & column_mask);
        shifted >>= column_bits_;

        uint64_t bank_mask = (bank_bits_ == 0) ? 0 : ((1ULL << bank_bits_) - 1);
        m.bank = (bank_bits_ == 0) ? 0 : static_cast<unsigned int>(shifted & bank_mask);
        shifted >>= bank_bits_;

        uint64_t channel_mask = (channel_bits_ == 0) ? 0 : ((1ULL << channel_bits_) - 1);
        m.channel = (channel_bits_ == 0) ? 0 : static_cast<unsigned int>(shifted & channel_mask);
        shifted >>= channel_bits_;

        m.row = shifted;
        return m;
    }

private:
    unsigned int column_bits_;
    unsigned int bank_bits_;
    unsigned int channel_bits_;
};

#endif
