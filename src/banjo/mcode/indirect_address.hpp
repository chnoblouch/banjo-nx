#ifndef MCODE_INDIRECT_ADDRESS_H
#define MCODE_INDIRECT_ADDRESS_H

#include "banjo/mcode/register.hpp"
#include "banjo/mcode/stack_frame.hpp"

#include <variant>

namespace banjo {

namespace mcode {

class IndirectAddress {

private:
    std::variant<Register, StackSlotID> base;
    std::variant<std::monostate, Register, int> offset;
    int scale;

public:
    IndirectAddress(std::variant<Register, StackSlotID> base) : base(base) {}

    IndirectAddress(std::variant<Register, StackSlotID> base, Register offset, int scale)
      : base(base),
        offset(offset),
        scale(scale) {}

    IndirectAddress(std::variant<Register, StackSlotID> base, int offset, int scale)
      : base(base),
        offset(offset),
        scale(scale) {}

    bool is_base_reg() { return base.index() == 0; }
    bool is_base_stack_slot() { return base.index() == 1; }

    Register get_base_reg() { return std::get<0>(base); }
    StackSlotID get_base_stack_slot() { return std::get<1>(base); }

    Register get_reg_offset() { return std::get<1>(offset); }
    int get_int_offset() { return std::get<2>(offset); }
    bool has_offset() { return offset.index() != 0; }
    bool has_reg_offset() { return offset.index() == 1; }
    bool has_int_offset() { return offset.index() == 2; }
    int get_scale() { return scale; }

    void set_base(Register base) { this->base = base; }
    void set_no_offset() { this->offset = std::monostate(); }
    void set_reg_offset(Register offset) { this->offset = offset; }
    void set_int_offset(int offset) { this->offset = offset; }
    void set_scale(int scale) { this->scale = scale; }

    friend bool operator==(const IndirectAddress &left, const IndirectAddress &right) {
        return left.base == right.base && left.offset == right.offset;
    }

    friend bool operator!=(const IndirectAddress &left, const IndirectAddress &right) { return !(left == right); }
};

} // namespace mcode

} // namespace banjo

#endif
