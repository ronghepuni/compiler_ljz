#include "BasicBlock.hpp"
#include "Constant.hpp"
#include "Function.hpp"
#include "GlobalVariable.hpp"
#include "Instruction.hpp"
#include "LICM.hpp"
#include "LoopDetection.hpp"
#include "PassManager.hpp"
#include <cstddef>
#include <memory>
#include <vector>

/**
 * @brief 循环不变式外提Pass的主入口函数
 * 
 */
void LoopInvariantCodeMotion::run() {

    loop_detection_ = std::make_unique<LoopDetection>(m_);
    loop_detection_->run();
    func_info_ = std::make_unique<FuncInfo>(m_);
    func_info_->run();
    for (auto &loop : loop_detection_->get_loops()) {
        is_loop_done_[loop] = false;
    }

    for (auto &loop : loop_detection_->get_loops()) {
        traverse_loop(loop);
    }
}

/**
 * @brief 遍历循环及其子循环
 * @param loop 当前要处理的循环
 * 
 */
void LoopInvariantCodeMotion::traverse_loop(std::shared_ptr<Loop> loop) {
    if (is_loop_done_[loop]) {
        return;
    }
    is_loop_done_[loop] = true;
    for (auto &sub_loop : loop->get_sub_loops()) {
        traverse_loop(sub_loop);
    }
    run_on_loop(loop);
}
// TODO: 收集循环信息, 实现collect_loop_info函数
// 1. 遍历当前循环及其子循环的所有指令
// 2. 收集所有指令到loop_instructions中
// 3. 检查store指令是否修改了全局变量，如果是则添加到updated_global中
// 4. 检查是否包含非纯函数调用，如果有则设置contains_impure_call为true
void LoopInvariantCodeMotion::collect_loop_info(
    std::shared_ptr<Loop> loop,
    std::set<Value *> &loop_instructions,
    std::set<Value *> &updated_global,
    bool &contains_impure_call) {
    
    for (auto &sub_loop : loop->get_sub_loops()) {
        collect_loop_info(sub_loop, loop_instructions, updated_global, contains_impure_call);
    }
    for (auto &bb : loop->get_blocks()) {
        for (auto &inst : bb->get_instructions()) {
            loop_instructions.insert(&inst);
            if (inst.get_instr_type() == Instruction::store) {
                auto *store_inst = dynamic_cast<StoreInst *>(&inst);
                if (auto *global = dynamic_cast<GlobalVariable*>(store_inst->get_lval())) {
                    updated_global.insert(global);
                }
            }
            if (inst.get_instr_type() == Instruction::call &&
                !func_info_->is_pure_function(dynamic_cast<Function *>(
                    dynamic_cast<CallInst *>(&inst)->get_operand(0)))) {
                contains_impure_call = true;
            }
        }
    }
    // throw std::runtime_error("Lab4: 你有一个TODO需要完成！");
}

/**
 * @brief 对单个循环执行不变式外提优化
 * @param loop 要优化的循环
 * 
 */
void LoopInvariantCodeMotion::run_on_loop(std::shared_ptr<Loop> loop) {
    std::set<Value *> loop_instructions;
    std::set<Value *> updated_global;
    bool contains_impure_call = false;
    collect_loop_info(loop, loop_instructions, updated_global, contains_impure_call);

    std::vector<Value *> loop_invariant;
    std::set<Value *> loop_inv_map;


    // TODO: 识别循环不变式指令
    //
    // - 如果指令已被标记为不变式则跳过
    // - 跳过 store、ret、br、phi 等指令与非纯函数调用
    // - 特殊处理全局变量的 load 指令
    // - 检查指令的所有操作数是否都是循环不变的
    // - 如果有新的不变式被添加则注意更新 changed 标志，继续迭代

    bool changed;
    do {
        changed = false;
        for (auto &bb : loop->get_blocks()) {
            for(auto &instr : bb->get_instructions()) {
                if (loop_inv_map.find(&instr) != loop_inv_map.end())
                    continue;
                auto instr_type = instr.get_instr_type();
                if (instr_type == Instruction::alloca ||
                    instr_type == Instruction::store ||
                    instr_type == Instruction::ret ||
                    instr_type == Instruction::br ||
                    instr_type == Instruction::phi)
                    continue;
                if (instr_type == Instruction::call &&
                    !func_info_->is_pure_function(dynamic_cast<Function *>(
                        dynamic_cast<CallInst *>(&instr)->get_operand(0))))
                    continue;
                
                if (instr_type == Instruction::load) {
                    if (dynamic_cast<GlobalVariable *>(
                        dynamic_cast<LoadInst *>(&instr)->get_lval()) == nullptr ||
                        updated_global.find(
                            dynamic_cast<LoadInst *>(&instr)->get_lval()) !=
                            updated_global.end() ||
                        contains_impure_call)
                        continue;
                }
                bool is_invariant = true;
                for (auto &op : instr.get_operands()) {
                    if ((loop_instructions.find(op) != loop_instructions.end() &&
                        loop_inv_map.find(op) == loop_inv_map.end())) {
                        is_invariant = false;
                        break;
                    }
                }
                if (is_invariant) {
                    loop_inv_map.insert(&instr);
                    loop_instructions.erase(&instr);
                    loop_invariant.push_back(&instr);
                    changed = true;
                }
            }

        }
        // throw std::runtime_error("Lab4: 你有一个TODO需要完成！");

    } while (changed);

    if (loop->get_preheader() == nullptr) {
        loop->set_preheader(
            BasicBlock::create(m_, "", loop->get_header()->get_parent()));
    }

    if (loop_inv_map.empty())
        return;

    // insert preheader
    auto preheader = loop->get_preheader();

    // TODO: 更新 phi 指令
    for (auto &phi_inst_ : loop->get_header()->get_instructions()) {
        if (phi_inst_.get_instr_type() != Instruction::phi)
            break;
        
        auto *phi_inst = dynamic_cast<PhiInst *>(&phi_inst_);
        std::vector<std::pair<Value *, BasicBlock *>> to_move;
        for (unsigned i = 0; i < phi_inst->get_num_operand(); i += 2) {
            auto *val = phi_inst->get_operand(i);
            auto *bb = dynamic_cast<BasicBlock *>(phi_inst->get_operand(i + 1));
            if (loop->get_latches().find(bb) != loop->get_latches().end())
                continue;
            to_move.push_back({val, bb});
        }

        if(to_move.size() == 0)    
            continue;
        auto *new_phi_inst =
            PhiInst::create_phi(phi_inst->get_type(), preheader);
        preheader->add_instruction(new_phi_inst);

        for (auto &pair : to_move) {
            phi_inst->remove_phi_operand(pair.second);
            new_phi_inst->add_phi_pair_operand(pair.first, pair.second);
        }

        phi_inst->add_phi_pair_operand(new_phi_inst, preheader);
        // throw std::runtime_error("Lab4: 你有一个TODO需要完成！");
    }

    // TODO: 用跳转指令重构控制流图 
    // 将所有非 latch 的 header 前驱块的跳转指向 preheader
    // 并将 preheader 的跳转指向 header
    // 注意这里需要更新前驱块的后继和后继的前驱
    std::vector<BasicBlock *> pred_to_remove;
    for (auto &pred : loop->get_header()->get_pre_basic_blocks()) {
        // throw std::runtime_error("Lab4: 你有一个TODO需要完成！");
        if (loop->get_latches().find(pred) != loop->get_latches().end())
            continue;
        auto *term = pred->get_terminator();
        for (unsigned i = 0; i < term->get_num_operand(); i++) {
            if (term->get_operand(i) == loop->get_header()) {
                term->set_operand(i, preheader);
            }
        }
        pred->remove_succ_basic_block(loop->get_header());
        pred->add_succ_basic_block(preheader);
        preheader->add_pre_basic_block(pred);
        pred_to_remove.push_back(pred);
    }

    for (auto &pred : pred_to_remove) {
        loop->get_header()->remove_pre_basic_block(pred);
    }

    // TODO: 外提循环不变指令
    // throw std::runtime_error("Lab4: 你有一个TODO需要完成！");
    for (auto &inst_ : loop_invariant) {
        auto *inst = dynamic_cast<Instruction *>(inst_);
        inst->get_parent()->remove_instr(inst);
        preheader->add_instruction(inst);
    }

    // insert preheader br to header
    BranchInst::create_br(loop->get_header(), preheader);

    // insert preheader to parent loop
    if (loop->get_parent() != nullptr) {
        loop->get_parent()->add_block(preheader);
    }
}

