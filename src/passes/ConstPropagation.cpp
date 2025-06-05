#include "ConstPropagation.hpp"
#include "Instruction.hpp"
#include "logging.hpp"

// 计算整数二元运算的常量折叠
ConstantInt *ConstFolder::compute(Instruction::OpID op, ConstantInt *value1,
                                  ConstantInt *value2) {
    int c_value1 = value1->get_value();
    int c_value2 = value2->get_value();
    switch (op) {
    case Instruction::add:
        return ConstantInt::get(c_value1 + c_value2, module_);
    case Instruction::sub:
        return ConstantInt::get(c_value1 - c_value2, module_);
    case Instruction::mul:
        return ConstantInt::get(c_value1 * c_value2, module_);
    case Instruction::sdiv:
        return c_value2 != 0 ? ConstantInt::get(c_value1 / c_value2, module_)
                             : nullptr;
    case Instruction::eq:
        return ConstantInt::get(c_value1 == c_value2, module_);
    case Instruction::ne:
        return ConstantInt::get(c_value1 != c_value2, module_);
    case Instruction::gt:
        return ConstantInt::get(c_value1 > c_value2, module_);
    case Instruction::ge:
        return ConstantInt::get(c_value1 >= c_value2, module_);
    case Instruction::lt:
        return ConstantInt::get(c_value1 < c_value2, module_);
    case Instruction::le:
        return ConstantInt::get(c_value1 <= c_value2, module_);
    default:
        return nullptr;
    }
}

// 计算浮点二元运算的常量折叠
ConstantFP *ConstFolder::compute(Instruction::OpID op, ConstantFP *value1,
                                 ConstantFP *value2) {
    float c_value1 = value1->get_value();
    float c_value2 = value2->get_value();
    switch (op) {
    case Instruction::fadd:
        return ConstantFP::get(c_value1 + c_value2, module_);
    case Instruction::fsub:
        return ConstantFP::get(c_value1 - c_value2, module_);
    case Instruction::fmul:
        return ConstantFP::get(c_value1 * c_value2, module_);
    case Instruction::fdiv:
        return c_value2 != 0.0f ? ConstantFP::get(c_value1 / c_value2, module_)
                                : nullptr;
    case Instruction::feq:
        return ConstantFP::get(c_value1 == c_value2, module_);
    case Instruction::fne:
        return ConstantFP::get(c_value1 != c_value2, module_);
    case Instruction::fgt:
        return ConstantFP::get(c_value1 > c_value2, module_);
    case Instruction::fge:
        return ConstantFP::get(c_value1 >= c_value2, module_);
    case Instruction::flt:
        return ConstantFP::get(c_value1 < c_value2, module_);
    case Instruction::fle:
        return ConstantFP::get(c_value1 <= c_value2, module_);
    default:
        return nullptr;
    }
}

// 计算整数到浮点的类型转换
ConstantFP *ConstFolder::compute(Instruction::OpID op, ConstantInt *value1) {
    int c_value1 = value1->get_value();
    switch (op) {
    case Instruction::sitofp:
        return ConstantFP::get(static_cast<float>(c_value1), module_);
    default:
        return nullptr;
    }
}

// 计算浮点到整数的类型转换
ConstantInt *ConstFolder::compute(Instruction::OpID op, ConstantFP *value1) {
    float c_value1 = value1->get_value();
    switch (op) {
    case Instruction::fptosi:
        return ConstantInt::get(static_cast<int>(c_value1), module_);
    default:
        return nullptr;
    }
}

// 尝试将 Value 转换为 ConstantFP
ConstantFP *cast_constantfp(Value *value) {
    auto constant_fp_ptr = dynamic_cast<ConstantFP *>(value);
    return constant_fp_ptr ? constant_fp_ptr : nullptr;
}

// 尝试将 Value 转换为 ConstantInt
ConstantInt *cast_constantint(Value *value) {
    auto constant_int_ptr = dynamic_cast<ConstantInt *>(value);
    return constant_int_ptr ? constant_int_ptr : nullptr;
}

// 常量传播主函数，仅进行常量合并
void ConstPropagation::run() {
    // 遍历所有函数
    for (auto &func : m_->get_functions()) {
        // 遍历所有基本块
        for (auto &bb : func.get_basic_blocks()) {
            builder->set_insert_point(&bb); // 设置插入点

            // 遍历基本块中的所有指令
            for (auto &instr : bb.get_instructions()) {
                // 处理整数二元运算（add, sub, mul, sdiv）
                if (instr.is_add() || instr.is_sub() || instr.is_mul() ||
                    instr.is_div()) {
                    auto value1 = cast_constantint(instr.get_operand(0));
                    auto value2 = cast_constantint(instr.get_operand(1));
                    if (value1 && value2) {
                        auto fold_const = folder->compute(
                            instr.get_instr_type(), value1, value2);
                        if (fold_const) {
                            instr.replace_all_use_with(
                                fold_const); // 用常量替换指令
                        }
                    }
                }
                // 处理浮点二元运算（fadd, fsub, fmul, fdiv）
                else if (instr.is_fadd() || instr.is_fsub() ||
                         instr.is_fmul() || instr.is_fdiv()) {
                    auto value1 = cast_constantfp(instr.get_operand(0));
                    auto value2 = cast_constantfp(instr.get_operand(1));
                    if (value1 && value2) {
                        auto fold_const = folder->compute(
                            instr.get_instr_type(), value1, value2);
                        if (fold_const) {
                            instr.replace_all_use_with(fold_const);
                        }
                    }
                }
                // 处理整数比较（eq, ne, gt, ge, lt, le）
                else if (instr.is_cmp()) {
                    auto value1 = cast_constantint(instr.get_operand(0));
                    auto value2 = cast_constantint(instr.get_operand(1));
                    if (value1 && value2) {
                        auto fold_const = folder->compute(
                            instr.get_instr_type(), value1, value2);
                        if (fold_const) {
                            instr.replace_all_use_with(fold_const);
                        }
                    }
                }
                // 处理类型转换（sitofp）
                else if (instr.is_si2fp()) {
                    auto value1 = cast_constantint(instr.get_operand(0));
                    if (value1) {
                        auto fold_const =
                            folder->compute(instr.get_instr_type(), value1);
                        if (fold_const) {
                            instr.replace_all_use_with(fold_const);
                        }
                    }
                }
                // 处理类型转换（fptosi）
                else if (instr.is_fp2si()) {
                    auto value1 = cast_constantfp(instr.get_operand(0));
                    if (value1) {
                        auto fold_const =
                            folder->compute(instr.get_instr_type(), value1);
                        if (fold_const) {
                            instr.replace_all_use_with(fold_const);
                        }
                    }
                }
            }
        }
    }
}