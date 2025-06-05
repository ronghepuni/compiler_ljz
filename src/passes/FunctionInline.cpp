#include "../../include/passes/FunctionInline.hpp"
#include "../../include/lightir/Function.hpp"

#include "BasicBlock.hpp"
#include "Instruction.hpp"
#include "Value.hpp"
#include "logging.hpp"
#include <cassert>
#include <utility>
#include <vector>

void FunctionInline::run() { inline_all_functions(); }

void FunctionInline::inline_all_functions() {
    // 检测递归函数
    std::set<Function *> recursive_func;
    for (auto &func : m_->get_functions()) {
        for (auto &bb : func.get_basic_blocks()) {
            for (auto &inst : bb.get_instructions()) {
                if (inst.is_call()) {
                    auto call = &inst;
                    auto func1 = static_cast<Function *>(call->get_operand(0));
                    if (func1 == &func) {
                        recursive_func.insert(func1);
                        break;
                    }
                }
            }
        }
    }
    // 遍历所有函数进行内联
    for (auto &func : m_->get_functions()) {
        // 跳过外部函数（如 output, input）
        if (outside_func.find(func.get_name()) != outside_func.end()) {
            continue;
        }
    a1:
        for (auto &bb : func.get_basic_blocks()) {
            for (auto &inst : bb.get_instructions()) {
                if (inst.is_call()) {
                    auto call = &inst;
                    auto func1 = static_cast<Function *>(call->get_operand(0));
                    // 跳过递归、外部或过大函数
                    if (func1 == &func)
                        continue;
                    if (recursive_func.find(func1) != recursive_func.end())
                        continue;
                    if (outside_func.find(func1->get_name()) !=
                        outside_func.end())
                        continue;
                    if (func1->get_basic_blocks().size() >= 6)
                        continue;
                    inline_function(call, func1);
                    goto a1; // 内联后重新扫描
                }
            }
        }
    }
}

void FunctionInline::inline_function(Instruction *call, Function *origin) {
    // 值映射表：原函数的值 -> 新复制的值
    std::map<Value *, Value *> v_map;
    // 新基本块列表
    std::vector<BasicBlock *> bb_list;
    // 返回指令列表（非 void 返回）
    std::vector<Instruction *> ret_list;
    // 参数映射：原函数参数 -> 调用点实参
    for (auto &arg : origin->get_args()) {
        v_map.insert(std::make_pair(static_cast<Value *>(&arg),
                                    call->get_operand(arg.get_arg_no() + 1)));
    }
    // 获取调用点基本块和函数
    auto call_bb = call->get_parent();
    auto call_func = call_bb->get_parent();
    // void 返回的基本块列表
    std::vector<BasicBlock *> ret_void_bbs;
    // 复制原函数的基本块和指令
    for (auto &bb : origin->get_basic_blocks()) {
        // 创建新基本块
        auto bb_new =
            BasicBlock::create(call_func->get_parent(), "", call_func);
        v_map.insert(std::make_pair(static_cast<Value *>(&bb),
                                    static_cast<Value *>(bb_new)));
        bb_list.push_back(bb_new);
        for (auto &inst : bb.get_instructions()) {
            // 处理 void 返回的 ret 指令
            if (inst.is_ret() && origin->get_return_type()->is_void_type()) {
                ret_void_bbs.push_back(bb_new);
                continue;
            }
            // 跳过 phi 指令（现有代码逻辑）
            if (inst.is_phi()) {
                ;
            }
            Instruction *inst_new;
            // 特殊处理 call 指令
            if (inst.is_call()) {
                auto call = static_cast<CallInst *>(&inst);
                auto func = static_cast<Function *>(call->get_operand(0));
                inst_new = new CallInst(func,
                                        {call->get_operands().begin() + 1,
                                         call->get_operands().end()},
                                        bb_new);
            } else {
                // 其他指令通过 clone 复制（如 getelementptr, sub）
                inst_new = inst.clone(bb_new);
            }
            // phi 指令插入到基本块开头
            if (inst.is_phi()) {
                bb_new->add_instr_begin(inst_new);
            }
            // 记录值映射
            v_map.insert(std::make_pair(static_cast<Value *>(&inst),
                                        static_cast<Value *>(inst_new)));
            // 收集返回指令
            if (inst.is_ret()) {
                ret_list.push_back(inst_new);
            }
        }
    }
    // 更新新指令的操作数
    for (auto bb : bb_list) {
        for (auto &inst : bb->get_instructions()) {
            for (int i = 0; i < inst.get_num_operand(); i++) {
                if (inst.is_phi()) {
                    ; // 跳过 phi 操作数更新（现有代码逻辑）
                }
                auto op = inst.get_operand(i);
                if (v_map.find(op) != v_map.end()) {
                    inst.set_operand(i, v_map[op]);
                }
            }
        }
    }
    // 返回值初始化
    Value *ret_val = nullptr;
    // 控制流终止标志
    bool is_terminated = false;
    // 创建新基本块，用于收集内联后的指令
    auto bb_new = BasicBlock::create(call_func->get_parent(), "", call_func);
    // 处理返回值
    if (!origin->get_return_type()->is_void_type()) {
        if (ret_list.size() == 1) {
            // 单返回值情况
            auto ret = ret_list.front();
            ret_val = ret->get_operand(0); // 获取返回值
            auto ret_bb = ret->get_parent();
            ret_bb->remove_instr(ret);             // 移除 ret 指令
            BranchInst::create_br(bb_new, ret_bb); // 跳转到 bb_new
        } else {
            // 多返回值情况（TODO 实现）
            // 1. 创建 bb_phi 用于存放 phi 指令
            auto bb_phi =
                BasicBlock::create(call_func->get_parent(), "", call_func);
            // 2. 处理每个返回指令
            std::vector<BasicBlock *> ret_bb_list; // 记录返回基本块
            for (auto ret : ret_list) {
                auto ret_bb = ret->get_parent();
                ret_bb_list.push_back(ret_bb);         // 记录基本块
                ret_bb->remove_instr(ret);             // 移除 ret 指令
                BranchInst::create_br(bb_phi, ret_bb); // 添加跳转到 bb_phi
            }
            // 3. 创建 phi 指令，设置返回类型
            auto phi = PhiInst::create_phi(origin->get_return_type(), bb_phi);
            // 为每个返回路径添加 [value, pred_bb] 对
            for (int i = 0; i < ret_list.size(); i++) {
                phi->add_phi_pair_operand(ret_list[i]->get_operand(0),
                                          ret_bb_list[i]);
            }
            // 4. 将 phi 指令添加到 bb_phi
            bb_phi->add_instruction(phi);
            // 5. 设置返回值
            ret_val = phi;
            // 6. 将 bb_phi 添加到 bb_list
            bb_list.push_back(bb_phi);
            // 7. 添加从 bb_phi 到 bb_new 的跳转
            BranchInst::create_br(bb_new, bb_phi);
        }
    } else {
        // void 返回情况
        assert(ret_void_bbs.size() > 0); // 确保有 void 返回基本块
        for (auto bb : ret_void_bbs) {
            BranchInst::create_br(bb_new, bb); // 跳转到 bb_new
        }
    }
    // 删除和移动调用点基本块的指令
    std::vector<Instruction *> del_list;
    BranchInst *br = nullptr;
    for (auto &inst : call_bb->get_instructions()) {
        if (!is_terminated) {
            if (&inst == call) {
                // 在调用点插入跳转到内联函数的第一个基本块
                br = BranchInst::create_br(bb_list.front(), call_bb);
                // 替换调用指令的用法
                if (!origin->get_return_type()->is_void_type()) {
                    call->replace_all_use_with(ret_val);
                }
                is_terminated = true;
            }
        } else {
            // 收集调用点后的指令
            if (dynamic_cast<BranchInst *>(&inst) == br) {
                continue;
            }
            del_list.push_back(&inst);
        }
    }
    // 移除调用指令
    call_bb->remove_instr(call);
    origin->remove_use(call, 0);
    // 移动后续指令到 bb_new
    for (auto inst : del_list) {
        call_bb->remove_instr(inst);
        bb_new->add_instruction(inst);
        inst->set_parent(bb_new);
    }
    // 重置控制流图
    origin->reset_bbs();
    call_func->reset_bbs();
}