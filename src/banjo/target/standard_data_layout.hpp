#ifndef STANDARD_DATA_LAYOUT_H
#define STANDARD_DATA_LAYOUT_H

#include "banjo/target/target_data_layout.hpp"

namespace banjo {

namespace target {

class StandardDataLayout final : public TargetDataLayout {

public:
    StandardDataLayout(unsigned usize, ssa::Type usize_type);
    unsigned get_size(const ssa::Type &type) const override;
    unsigned get_alignment(const ssa::Type &type) const override;
    unsigned get_member_offset(ssa::Structure *struct_, unsigned index) const override;
};

} // namespace target

} // namespace banjo

#endif
