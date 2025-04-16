#include "cminusf_builder.hpp"

// 定义常量生成宏，用于快速创建浮点数和整数常量
#define CONST_FP(num) ConstantFP::get((float)num, module.get())
#define CONST_INT(num) ConstantInt::get(num, module.get())

// 类型定义：全局类型指针，用于表示 LLVM IR 中的基本类型
Type *VOID_T;       // void 类型
Type *INT1_T;       // 1 位整数类型（布尔值）
Type *INT32_T;      // 32 位整数类型
Type *INT32PTR_T;   // 32 位整数指针类型
Type *FLOAT_T;      // 浮点数类型
Type *FLOATPTR_T;   // 浮点数指针类型

// 类型检查宏：用于快速判断值的类型
#define is_POINTER_INTEGER(cur_val) ((cur_val)->get_type()->get_pointer_element_type()->is_integer_type())  // 检查是否为整数指针
#define is_POINTER_FLOAT(cur_val) ((cur_val)->get_type()->get_pointer_element_type()->is_float_type())      // 检查是否为浮点数指针
#define is_POINTER_POINTER(cur_val) ((cur_val)->get_type()->get_pointer_element_type()->is_pointer_type())   // 检查是否为指针的指针
#define is_INTEGER(cur_val) ((cur_val)->get_type()->is_integer_type())                                      // 检查是否为整数
#define is_FLOAT(cur_val) ((cur_val)->get_type()->is_float_type())                                          // 检查是否为浮点数
#define is_POINTER(cur_val) ((cur_val)->get_type()->is_pointer_type())                                      // 检查是否为指针

// 访问 ASTProgram 节点：处理程序的顶层结构
Value* CminusfBuilder::visit(ASTProgram &node) {
    // 初始化基本类型：从模块中获取 LLVM IR 的基本类型
    VOID_T = module->get_void_type();       // 获取 void 类型
    INT1_T = module->get_int1_type();       // 获取 1 位整数类型
    INT32_T = module->get_int32_type();     // 获取 32 位整数类型
    INT32PTR_T = module->get_int32_ptr_type(); // 获取 32 位整数指针类型
    FLOAT_T = module->get_float_type();     // 获取浮点数类型
    FLOATPTR_T = module->get_float_ptr_type(); // 获取浮点数指针类型

    Value *ret_val = nullptr;               // 初始化返回值，记录最后处理的声明
    // 遍历所有声明：处理程序中的全局变量和函数声明
    for (auto &decl : node.declarations) {
        ret_val = decl->accept(*this);      // 递归访问每个声明节点
    }
    return ret_val;                         // 返回最后处理的声明值（通常为 nullptr）
}

// 访问 ASTNum 节点：处理数值常量
Value* CminusfBuilder::visit(ASTNum &node) {
    // 处理数值节点：根据节点类型生成整数或浮点数常量
    if(node.type==TYPE_INT) {
        context.val = CONST_INT(node.i_val); // 整型常量：生成 LLVM 整数常量
    }
    else if(node.type==TYPE_FLOAT) {
        context.val = CONST_FP(node.f_val);  // 浮点型常量：生成 LLVM 浮点数常量
    }
    return nullptr;                         // 返回空指针，数值节点不产生直接返回值
}

// 访问 ASTVarDeclaration 节点：处理变量声明
Value* CminusfBuilder::visit(ASTVarDeclaration &node) {
    // 变量声明处理：确定变量类型
    Type* var_type;
    if(node.type==TYPE_INT) {
        var_type = INT32_T;                 // 整型：设置为 32 位整数类型
    }
    else if(node.type==TYPE_FLOAT) {
        var_type = FLOAT_T;                 // 浮点型：设置为浮点数类型
    }

    // 根据作用域处理全局或局部变量
    if(scope.in_global()) {
        // 全局作用域
        if (node.num==nullptr) {            // 单一变量（非数组）
            auto init_val = ConstantZero::get(var_type, module.get()); // 创建类型对应的零初始化值
            auto gen_var = GlobalVariable::create(node.id, module.get(), var_type, false, init_val); // 创建全局变量
            scope.push(node.id, gen_var);   // 将变量加入全局作用域
        }
        else {                              // 数组类型
            auto* arr_ptr = ArrayType::get(var_type, node.num->i_val); // 创建指定大小的数组类型
            auto init_val = ConstantZero::get(arr_ptr, module.get()); // 创建数组的零初始化值
            auto gen_arr = GlobalVariable::create(node.id, module.get(), arr_ptr, false, init_val); // 创建全局数组
            scope.push(node.id, gen_arr);   // 将数组加入全局作用域
        }
    }
    else {
        // 局部作用域
        if(node.num==nullptr) {
            auto gen_var = builder->create_alloca(var_type); // 局部变量分配：分配栈上空间
            scope.push(node.id, gen_var);   // 将变量加入局部作用域
        }
        else {
            auto* arr_ptr = ArrayType::get(var_type, node.num->i_val); // 创建局部数组类型
            auto gen_arr = builder->create_alloca(arr_ptr); // 分配数组的栈上空间
            scope.push(node.id, gen_arr);   // 将数组加入局部作用域
        }
    }
    return nullptr;                         // 返回空指针，变量声明不产生直接返回值
}

// 访问 ASTFunDeclaration 节点：处理函数声明
Value* CminusfBuilder::visit(ASTFunDeclaration &node) {
    // 函数声明处理：确定函数返回类型
    FunctionType *fun_type;
    Type *ret_type;
    std::vector<Type *> param_types;
    if (node.type == TYPE_INT)
        ret_type = INT32_T;                 // 返回类型为整数
    else if (node.type == TYPE_FLOAT)
        ret_type = FLOAT_T;                 // 返回类型为浮点数
    else
        ret_type = VOID_T;                  // 返回类型为 void

    // 处理参数类型：根据参数类型和是否为数组确定 LLVM 类型
    for (auto param : node.params) {
        if (param->type==TYPE_INT) {
            if (param->isarray) 
                param_types.push_back(INT32PTR_T); // 整数数组参数：使用整数指针类型
            else 
                param_types.push_back(INT32_T); // 整数参数：使用整数类型
        }
        else {
            if (param->isarray) 
                param_types.push_back(FLOATPTR_T); // 浮点数数组参数：使用浮点数指针类型
            else 
                param_types.push_back(FLOAT_T); // 浮点数参数：使用浮点数类型
        }
    }

    // 创建函数类型和函数
    fun_type = FunctionType::get(ret_type, param_types); // 创建函数类型
    auto func = Function::create(fun_type, node.id, module.get()); // 创建函数定义
    scope.push(node.id, func);              // 将函数加入作用域
    context.func = func;                    // 设置当前函数上下文
    auto funBB = BasicBlock::create(module.get(), "entry", func); // 创建函数入口基本块
    builder->set_insert_point(funBB);       // 设置 IR 插入点
    scope.enter();                          // 进入新的作用域

    // 处理函数参数：为每个参数分配存储空间
    std::vector<Value *> args;
    for (auto &arg : func->get_args()) {
        args.push_back(&arg);               // 收集函数参数
    }
    Value* var_alloca;
    for (int i = 0; i < node.params.size(); ++i) {
        auto param = node.params[i];
        if (param->type==TYPE_INT) {
            if (param->isarray) {
                var_alloca = builder->create_alloca(INT32PTR_T); // 为整数数组参数分配指针空间
                builder->create_store(args[i], var_alloca); // 存储参数值
                scope.push(param->id, var_alloca); // 加入作用域
            }
            else {
                var_alloca = builder->create_alloca(INT32_T); // 为整数参数分配空间
                builder->create_store(args[i], var_alloca); // 存储参数值
                scope.push(param->id, var_alloca); // 加入作用域
            }
        }
        else {
            if (param->isarray) {
                var_alloca = builder->create_alloca(FLOATPTR_T); // 为浮点数数组参数分配指针空间
                builder->create_store(args[i], var_alloca); // 存储参数值
                scope.push(param->id, var_alloca); // 加入作用域
            }
            else {
                var_alloca = builder->create_alloca(FLOAT_T); // 为浮点数参数分配空间
                builder->create_store(args[i], var_alloca); // 存储参数值
                scope.push(param->id, var_alloca); // 加入作用域
            }
        }
    }

    // 访问函数体复合语句
    node.compound_stmt->accept(*this);      // 递归处理函数体
    // 处理函数返回：如果基本块未终止，生成默认返回
    if (!builder->get_insert_block()->is_terminated()) {
        if (context.func->get_return_type()->is_void_type())
            builder->create_void_ret();      // void 函数生成空返回
        else if (context.func->get_return_type()->is_float_type())
            builder->create_ret(CONST_FP(0.)); // 浮点数函数返回 0.0
        else
            builder->create_ret(CONST_INT(0)); // 整数函数返回 0
    }
    scope.exit();                           // 退出函数作用域
    return nullptr;                         // 返回空指针，函数声明不产生直接返回值
}

// 访问 ASTParam 节点：处理函数参数
Value* CminusfBuilder::visit(ASTParam &node) {
    return nullptr;                         // 参数节点不产生直接返回值
}

// 访问 ASTCompoundStmt 节点：处理复合语句
Value* CminusfBuilder::visit(ASTCompoundStmt &node) {
    // 复合语句处理：处理局部声明和语句列表
    scope.enter();                          // 进入新的作用域
    for (auto &decl : node.local_declarations) {
        decl->accept(*this);                // 递归处理局部变量声明
    }
    for (auto &stmt : node.statement_list) {
        stmt->accept(*this);                // 递归处理语句
        if (builder->get_insert_block()->is_terminated())
            break;                          // 如果基本块已终止，停止处理
    }
    scope.exit();                           // 退出作用域
    return nullptr;                         // 返回空指针，复合语句不产生直接返回值
}

// 访问 ASTExpressionStmt 节点：处理表达式语句
Value* CminusfBuilder::visit(ASTExpressionStmt &node) {
    // 表达式语句：处理表达式
    if (node.expression != nullptr)
        node.expression->accept(*this);     // 递归处理表达式
    return nullptr;                         // 返回空指针，表达式语句不产生直接返回值
}

// 访问 ASTSelectionStmt 节点：处理选择语句（if-else）
Value* CminusfBuilder::visit(ASTSelectionStmt &node) {
    // if-else语句处理：生成条件分支
    node.expression->accept(*this);         // 访问条件表达式
    auto cur_val = context.val;             // 获取条件表达式的值
    auto trueBB = BasicBlock::create(module.get(), "", context.func); // 创建真分支基本块
    auto falseBB = BasicBlock::create(module.get(), "", context.func); // 创建假分支基本块
    auto endBB = BasicBlock::create(module.get(), "", context.func); // 创建结束基本块

    Value *cond_val;
    // 根据表达式类型生成比较指令
    if (is_FLOAT(cur_val))
        cond_val = builder->create_fcmp_ne(cur_val, CONST_FP(0.)); // 浮点数非零比较
    else
        cond_val = builder->create_icmp_ne(cur_val, CONST_INT(0)); // 整数非零比较

    // 创建条件分支
    if (node.else_statement != nullptr)
        builder->create_cond_br(cond_val, trueBB, falseBB); // 有 else 分支
    else
        builder->create_cond_br(cond_val, trueBB, endBB); // 无 else 分支

    // 处理真分支
    builder->set_insert_point(trueBB);      // 设置插入点为真分支
    node.if_statement->accept(*this);        // 访问 if 语句
    if (!builder->get_insert_block()->is_terminated())
        builder->create_br(endBB);           // 如果未终止，跳转到结束

    // 处理假分支
    if (node.else_statement != nullptr) {
        builder->set_insert_point(falseBB);  // 设置插入点为假分支
        node.else_statement->accept(*this);  // 访问 else 语句
        if (!builder->get_insert_block()->is_terminated())
            builder->create_br(endBB);       // 如果未终止，跳转到结束
    }
    else
        falseBB->erase_from_parent();       // 删除未使用的假分支

    builder->set_insert_point(endBB);        // 设置插入点为结束基本块
    return nullptr;                         // 返回空指针，选择语句不产生直接返回值
}

// 访问 ASTIterationStmt 节点：处理迭代语句（while）
Value* CminusfBuilder::visit(ASTIterationStmt &node) {
    // while循环处理：生成循环结构
    auto cur_fun = context.func;            // 获取当前函数
    auto judgeBB = BasicBlock::create(module.get(), "", cur_fun); // 创建条件判断基本块
    auto stmtBB = BasicBlock::create(module.get(), "", cur_fun); // 创建循环体基本块
    auto endBB = BasicBlock::create(module.get(), "", cur_fun); // 创建循环结束基本块
    if (!builder->get_insert_block()->is_terminated())
        builder->create_br(judgeBB);        // 如果当前基本块未终止，跳转到判断块

    // 处理条件判断
    builder->set_insert_point(judgeBB);     // 设置插入点为判断块
    node.expression->accept(*this);         // 访问循环条件表达式
    
    auto cur_val = context.val;             // 获取条件表达式的值
    Value* cond_val;
    // 根据表达式类型生成比较指令
    if (cur_val->get_type()->is_float_type())
        cond_val = builder->create_fcmp_ne(cur_val, CONST_FP(0.)); // 浮点数非零比较
    else
        cond_val = builder->create_icmp_ne(cur_val, CONST_INT(0)); // 整数非零比较

    builder->create_cond_br(cond_val, stmtBB, endBB); // 创建条件分支：真跳转到循环体，假跳转到结束

    // 处理循环体
    builder->set_insert_point(stmtBB);      // 设置插入点为循环体
    node.statement->accept(*this);           // 访问循环体语句
    if (!builder->get_insert_block()->is_terminated())
        builder->create_br(judgeBB);         // 如果未终止，跳转回判断块

    builder->set_insert_point(endBB);        // 设置插入点为结束块
    return nullptr;                         // 返回空指针，迭代语句不产生直接返回值
}

// 访问 ASTReturnStmt 节点：处理返回语句
Value* CminusfBuilder::visit(ASTReturnStmt &node) {
    // return语句处理：生成返回指令
    if (node.expression == nullptr) {
        builder->create_void_ret();         // 空返回：生成 void 返回指令
        return nullptr;                     // 返回空指针
    } else {
        // 非空返回：处理返回值表达式
        auto cur_fun = context.func;        // 获取当前函数
        auto ret_type = cur_fun->get_function_type()->get_return_type(); // 获取函数返回类型
        node.expression->accept(*this);     // 访问返回表达式
        auto cur_val = context.val;         // 获取表达式值
        if (ret_type->is_integer_type()) {
            // 返回类型为整数
            if (ret_type != cur_val->get_type())
                cur_val = builder->create_fptosi(cur_val, INT32_T); // 如果类型不匹配，转换为整数
        }
        else {
            // 返回类型为浮点数或 void
            if (ret_type != cur_val->get_type())
                cur_val = builder->create_sitofp(cur_val, FLOAT_T); // 如果类型不匹配，转换为浮点数
        }
        builder->create_ret(cur_val);       // 生成返回指令
    }
    return nullptr;                         // 返回空指针，返回语句不产生直接返回值
}

// 访问 ASTVar 节点：处理变量引用
Value* CminusfBuilder::visit(ASTVar &node) {
    // 变量引用处理：处理变量或数组元素
    bool is_lval = context.is_lval;         // 保存当前是否为左值
    context.is_lval = false;                // 重置左值标志
    if (node.expression == nullptr) {
        // 单一变量引用
        auto cur_var = scope.find(node.id); // 在作用域中查找变量
        if (is_lval)
            context.val = cur_var;          // 左值：直接使用变量地址
        else {
            // 右值：加载变量值或获取数组首地址
            if (is_POINTER_FLOAT(cur_var) || is_POINTER_INTEGER(cur_var) || is_POINTER_POINTER(cur_var))
                context.val = builder->create_load(cur_var); // 加载指针指向的值
            else
                context.val = builder->create_gep(cur_var, {CONST_INT(0), CONST_INT(0)}); // 获取数组首地址
        }
    }
    else {
        // 数组元素引用
        auto cur_var = scope.find(node.id); // 在作用域中查找数组
        node.expression->accept(*this);     // 访问数组索引表达式
        auto cur_val = context.val;         // 获取索引值
        if (cur_val->get_type()->is_float_type())
            cur_val = builder->create_fptosi(cur_val, INT32_T); // 如果索引为浮点数，转换为整数
        auto cur_fun = context.func;        // 获取当前函数
        auto condBB = BasicBlock::create(module.get(), "", cur_fun); // 创建正常索引基本块
        auto exceptBB = BasicBlock::create(module.get(), "", cur_fun); // 创建负索引异常基本块
        Value* is_neg = builder->create_icmp_lt(cur_val, CONST_INT(0)); // 检查索引是否为负
        builder->create_cond_br(is_neg, exceptBB, condBB); // 负索引跳转到异常块

        // 处理负索引异常
        builder->set_insert_point(exceptBB); // 设置插入点为异常块
        auto deal_fail = scope.find("neg_idx_except"); // 查找负索引异常处理函数
        builder->create_call(static_cast<Function *>(deal_fail), {}); // 调用异常处理函数

        // 根据函数返回类型生成默认返回
        if (cur_fun->get_return_type()->is_void_type())
            builder->create_void_ret();      // void 函数返回
        else if (cur_fun->get_return_type()->is_float_type())
            builder->create_ret(CONST_FP(0.)); // 浮点数函数返回 0.0
        else
            builder->create_ret(CONST_INT(0)); // 整数函数返回 0

        // 处理正常索引
        builder->set_insert_point(condBB);  // 设置插入点为正常索引块
        Value* new_ptr;
        if (is_POINTER_POINTER(cur_var)) {
            auto array_ptr = builder->create_load(cur_var); // 加载指针数组的基地址
            new_ptr = builder->create_gep(array_ptr, {cur_val}); // 计算元素地址
        }
        else if (is_POINTER_FLOAT(cur_var) || is_POINTER_INTEGER(cur_var)) {
            new_ptr = builder->create_gep(cur_var, {cur_val}); // 计算元素地址
        }
        else {
            new_ptr = builder->create_gep(cur_var, {CONST_INT(0), cur_val}); // 计算数组元素地址
        }

        if (is_lval) {
            context.is_lval = false;        // 重置左值标志
            context.val = new_ptr;          // 左值：返回元素地址
        }
        else {
            context.val = builder->create_load(new_ptr); // 右值：加载元素值
        }
    }
    return nullptr;                         // 返回空指针，变量引用不产生直接返回值
}

// 访问 ASTAssignExpression 节点：处理赋值表达式
Value* CminusfBuilder::visit(ASTAssignExpression &node) {
    // 赋值表达式处理：生成赋值指令
    node.expression->accept(*this);         // 访问右侧表达式
    context.is_lval = true;                 // 设置为左值模式
    auto value = context.val;               // 获取右侧表达式的值
    node.var->accept(*this);                // 访问左侧变量
    auto var_alloc = context.val;           // 获取左侧变量的地址
    // 类型检查与转换
    if (value->get_type() != var_alloc->get_type()->get_pointer_element_type()) {
        if (value->get_type() == INT32_T)
            value = builder->create_sitofp(value, FLOAT_T); // 整数转换为浮点数
        else
            value = builder->create_fptosi(value, INT32_T); // 浮点数转换为整数
    }
    builder->create_store(value, var_alloc); // 生成存储指令
    context.val = value;                    // 更新上下文值
    return nullptr;                         // 返回空指针，赋值表达式不产生直接返回值
}

// 访问 ASTSimpleExpression 节点：处理简单表达式（比较）
Value* CminusfBuilder::visit(ASTSimpleExpression &node) {
    // 简单表达式处理：处理比较运算
    if (node.additive_expression_r == nullptr)
        node.additive_expression_l->accept(*this); // 单一表达式：直接访问左侧表达式
    else {
        // 比较表达式：处理左右两侧
        node.additive_expression_l->accept(*this); // 访问左侧表达式
        auto lvalue = context.val;              // 获取左侧值
        node.additive_expression_r->accept(*this); // 访问右侧表达式
        auto rvalue = context.val;              // 获取右侧值
        bool is_int;
        // 类型检查与统一
        if (lvalue->get_type() == rvalue->get_type())
            is_int = rvalue->get_type()->is_integer_type(); // 类型相同，检查是否为整数
        else {
            if (is_INTEGER(lvalue))
                lvalue = builder->create_sitofp(lvalue, FLOAT_T); // 左侧整数转换为浮点数
            if (is_INTEGER(rvalue))
                rvalue = builder->create_sitofp(rvalue, FLOAT_T); // 右侧整数转换为浮点数
            is_int = false;                     // 类型不同，视为浮点数
        }

        Value* flag;
        // 根据操作符生成比较指令
        switch (node.op) {
            case OP_LT:
                if (is_int)
                    flag = builder->create_icmp_lt(lvalue, rvalue); // 整数小于比较
                else
                    flag = builder->create_fcmp_lt(lvalue, rvalue); // 浮点数小于比较
                break;
            case OP_LE:
                if (is_int)
                    flag = builder->create_icmp_le(lvalue, rvalue); // 整数小于等于比较
                else
                    flag = builder->create_fcmp_le(lvalue, rvalue); // 浮点数小于等于比较
                break;
            case OP_GE:
                if (is_int)
                    flag = builder->create_icmp_ge(lvalue, rvalue); // 整数大于等于比较
                else
                    flag = builder->create_fcmp_ge(lvalue, rvalue); // 浮点数大于等于比较
                break;
            case OP_GT:
                if (is_int)
                    flag = builder->create_icmp_gt(lvalue, rvalue); // 整数大于比较
                else
                    flag = builder->create_fcmp_gt(lvalue, rvalue); // 浮点数大于比较
                break;
            case OP_EQ:
                if (is_int)
                    flag = builder->create_icmp_eq(lvalue, rvalue); // 整数等于比较
                else
                    flag = builder->create_fcmp_eq(lvalue, rvalue); // 浮点数等于比较
                break;
            case OP_NEQ:
                if (is_int)
                    flag = builder->create_icmp_ne(lvalue, rvalue); // 整数不等于比较
                else
                    flag = builder->create_fcmp_ne(lvalue, rvalue); // 浮点数不等于比较
                break;
            default:
                break;                          // 默认情况：不处理
        }
        context.val = builder->create_zext(flag, INT32_T); // 将比较结果扩展为 32 位整数
    }
    return nullptr;                         // 返回空指针，简单表达式不产生直接返回值
}

// 访问 ASTAdditiveExpression 节点：处理加减表达式
Value* CminusfBuilder::visit(ASTAdditiveExpression &node) {
    // 加减表达式处理：处理加法或减法
    if (node.additive_expression == nullptr)
        node.term->accept(*this);           // 单一项：直接访问项
    else {
        // 加减运算：处理左右两侧
        node.additive_expression->accept(*this); // 访问左侧表达式
        auto lvalue = context.val;          // 获取左侧值
        node.term->accept(*this);           // 访问右侧项
        auto rvalue = context.val;          // 获取右侧值
        bool is_int;
        // 类型检查与统一
        if (lvalue->get_type() == rvalue->get_type())
            is_int = rvalue->get_type()->is_integer_type(); // 类型相同，检查是否为整数
        else {
            if (is_INTEGER(lvalue))
                lvalue = builder->create_sitofp(lvalue, FLOAT_T); // 左侧整数转换为浮点数
            if (is_INTEGER(rvalue))
                rvalue = builder->create_sitofp(rvalue, FLOAT_T); // 右侧整数转换为浮点数
            is_int = false;                 // 类型不同，视为浮点数
        }
        // 根据操作符生成运算指令
        switch (node.op) {
            case OP_PLUS:
                if (is_int)
                    context.val = builder->create_iadd(lvalue, rvalue); // 整数加法
                else
                    context.val = builder->create_fadd(lvalue, rvalue); // 浮点数加法
                break;
            case OP_MINUS:
                if (is_int)
                    context.val = builder->create_isub(lvalue, rvalue); // 整数减法
                else
                    context.val = builder->create_fsub(lvalue, rvalue); // 浮点数减法
                break;
            default:
                break;                      // 默认情况：不处理
        }
    }
    return nullptr;                         // 返回空指针，加减表达式不产生直接返回值
}

// 访问 ASTTerm 节点：处理乘除表达式
Value* CminusfBuilder::visit(ASTTerm &node) {
    // 乘除表达式处理：处理乘法或除法
    if (node.term == nullptr)
        node.factor->accept(*this);         // 单一因子：直接访问因子
    else {
        // 乘除运算：处理左右两侧
        node.term->accept(*this);           // 访问左侧项
        auto lvalue = context.val;          // 获取左侧值
        node.factor->accept(*this);         // 访问右侧因子
        auto rvalue = context.val;          // 获取右侧值
        bool is_int;
        // 类型检查与统一
        if (lvalue->get_type() == rvalue->get_type())
            is_int = rvalue->get_type()->is_integer_type(); // 类型相同，检查是否为整数
        else {
            if (is_INTEGER(lvalue))
                lvalue = builder->create_sitofp(lvalue, FLOAT_T); // 左侧整数转换为浮点数
            if (is_INTEGER(rvalue))
                rvalue = builder->create_sitofp(rvalue, FLOAT_T); // 右侧整数转换为浮点数
            is_int = false;                 // 类型不同，视为浮点数
        }
        // 根据操作符生成运算指令
        switch (node.op) {
            case OP_MUL:
                if (is_int)
                    context.val = builder->create_imul(lvalue, rvalue); // 整数乘法
                else
                    context.val = builder->create_fmul(lvalue, rvalue); // 浮点数乘法
                break;
            case OP_DIV:
                if (is_int)
                    context.val = builder->create_isdiv(lvalue, rvalue); // 整数除法
                else
                    context.val = builder->create_fdiv(lvalue, rvalue); // 浮点数除法
                break;
            default:
                break;                      // 默认情况：不处理
        }
    }
    return nullptr;                         // 返回空指针，乘除表达式不产生直接返回值
}

// 访问 ASTCall 节点：处理函数调用
Value* CminusfBuilder::visit(ASTCall &node) {
    // 函数调用处理：生成函数调用指令
    auto cur_fun = static_cast<Function *>(scope.find(node.id)); // 查找被调用函数
    auto params = cur_fun->get_function_type()->param_begin(); // 获取函数参数类型迭代器
    std::vector<Value *> args;               // 存储实际参数
    for (auto &arg : node.args) {
        arg->accept(*this);                  // 访问参数表达式
        auto cur_val = context.val;          // 获取参数值
        // 类型检查与转换
        if (!is_POINTER(cur_val) && *params != cur_val->get_type()) {
            if (is_INTEGER(cur_val))
                cur_val = builder->create_sitofp(cur_val, FLOAT_T); // 整数转换为浮点数
            else
                cur_val = builder->create_fptosi(cur_val, INT32_T); // 浮点数转换为整数
        }
        args.push_back(cur_val);             // 添加参数到参数列表
        params++;                            // 移动到下一个参数类型
    }
    context.val = builder->create_call(static_cast<Function *>(cur_fun), args); // 生成函数调用指令
    return nullptr;                         // 返回空指针，函数调用不产生直接返回值
}