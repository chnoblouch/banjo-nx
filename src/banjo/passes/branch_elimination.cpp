#include "branch_elimination.hpp"

#include "banjo/ssa/control_flow_graph.hpp"

#include <iostream>

namespace banjo {

namespace passes {

BranchElimination::BranchElimination(target::Target *target) : Pass("branch-elimination", target) {}

void BranchElimination::run(ssa::Module &mod) {
    for (ssa::Function *func : mod.get_functions()) {
        run(func);
    }
}

void BranchElimination::run(ssa::Function *func) {
    ssa::ControlFlowGraph cfg(func);

    for (ssa::BasicBlockIter iter = func->begin(); iter != func->end(); ++iter) {
        ssa::ControlFlowGraph::Node node = cfg.get_node(iter);
        if (node.successors.size() != 2) {
            continue;
        }

        ssa::InstrIter branch_instr = iter->get_instrs().get_last_iter();
        ssa::BasicBlockIter succ_0 = branch_instr->get_operand(3).get_branch_target().block;
        ssa::BasicBlockIter succ_1 = branch_instr->get_operand(4).get_branch_target().block;

        if (succ_0->get_instrs().get_size() != 1 || succ_1->get_instrs().get_size() != 1) {
            continue;
        }

        if (cfg.get_node(succ_0).predecessors.size() != 1 || cfg.get_node(succ_1).predecessors.size() != 1) {
            continue;
        }

        ssa::Instruction &instr_0 = succ_0->get_instrs().get_first();
        ssa::Instruction &instr_1 = succ_1->get_instrs().get_first();
        if (instr_0.get_opcode() != ssa::Opcode::JMP || instr_1.get_opcode() != ssa::Opcode::JMP) {
            continue;
        }

        ssa::BranchTarget &target_0 = instr_0.get_operand(0).get_branch_target();
        ssa::BranchTarget &target_1 = instr_1.get_operand(0).get_branch_target();
        if (target_0.block != target_1.block || target_0.args.size() != target_1.args.size()) {
            continue;
        }

        ssa::ControlFlowGraph::Node &join_node = cfg.get_node(target_0.block);

        unsigned unequal_arg_count = 0;
        unsigned unequal_arg_index = 0;

        for (unsigned i = 0; i < target_0.args.size(); i++) {
            if (target_0.args[i] != target_1.args[i]) {
                unequal_arg_count++;
                unequal_arg_index = i;
            }
        }

        if (unequal_arg_count != 1) {
            continue;
        }

        ssa::VirtualRegister dst;
        if (join_node.predecessors.size() == 2) {
            // If the entry and exit blocks will be merged, we use the param reg directly as the dst.
            dst = join_node.block->get_param_regs()[unequal_arg_index];
        } else {
            // Otherwise we have to create a new reg.
            dst = func->next_virtual_reg();
        }

        iter->append(ssa::Instruction(
            ssa::Opcode::SELECT,
            dst,
            {
                branch_instr->get_operand(0),
                branch_instr->get_operand(1),
                branch_instr->get_operand(2),
                target_0.args[unequal_arg_index],
                target_1.args[unequal_arg_index],
            }
        ));

        // Remove the conditional branch instruction.
        iter->remove(iter->get_instrs().get_last_iter().get_prev());

        if (join_node.predecessors.size() == 2) {
            // Merge the remaining blocks together if the join node is only used in this branch.
            func->merge_blocks(iter, join_node.block);
        } else {
            // Otherwise append a jump to the join node.
            ssa::BranchTarget target{.block = join_node.block, .args = target_0.args};
            target.args[unequal_arg_index].set_to_register(dst);
            iter->append(ssa::Instruction(ssa::Opcode::JMP, {ssa::Operand::from_branch_target(target)}));
        }

        // Remove the two conditional branch targets.
        func->get_basic_blocks().remove(succ_0);
        func->get_basic_blocks().remove(succ_1);

        // Update the control flow graph.
        cfg = ssa::ControlFlowGraph(func);

        // Continue in this block as there might be more opportunities for branch elimination.
        iter = iter.get_prev();
    }
}

} // namespace passes

} // namespace banjo
