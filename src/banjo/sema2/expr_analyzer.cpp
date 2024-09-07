#include "expr_analyzer.hpp"

#include "banjo/sema2/const_evaluator.hpp"
#include "banjo/sema2/decl_body_analyzer.hpp"
#include "banjo/sema2/decl_interface_analyzer.hpp"
#include "banjo/sema2/generics_specializer.hpp"
#include "banjo/sema2/magic_methods.hpp"
#include "banjo/sema2/meta_expr_evaluator.hpp"
#include "banjo/sir/sir.hpp"
#include "banjo/sir/sir_visitor.hpp"
#include "banjo/utils/macros.hpp"
#include "generic_arg_deduction.hpp"
#include "semantic_analyzer.hpp"
#include "use_resolver.hpp"

#include <cassert>
#include <vector>

namespace banjo {

namespace lang {

namespace sema {

ExprConstraints ExprConstraints::expect_type(sir::Expr type) {
    return {
        .expected_type = type,
    };
}

ExprAnalyzer::ExprAnalyzer(SemanticAnalyzer &analyzer) : analyzer(analyzer) {}

ExprAnalyzer::ExprAnalyzer(SemanticAnalyzer &analyzer, ExprConstraints constraints)
  : analyzer(analyzer),
    constraints(constraints) {}

Result ExprAnalyzer::analyze(sir::Expr &expr) {
    Result result;

    result = analyze_uncoerced(expr);
    if (result != Result::SUCCESS) {
        return result;
    }

    if (constraints.expected_type) {
        return finalize_type_by_coercion(expr, constraints.expected_type);
    } else {
        return finalize_type(expr);
    }
}

Result ExprAnalyzer::analyze_uncoerced(sir::Expr &expr) {
    Result result = Result::SUCCESS;

    SIR_VISIT_EXPR(
        expr,
        SIR_VISIT_IMPOSSIBLE,                                        // empty
        result = Result::SUCCESS,                                    // int_literal
        result = Result::SUCCESS,                                    // fp_literal
        analyze_bool_literal(*inner),                                // bool_literal
        analyze_char_literal(*inner),                                // char_literal
        result = Result::SUCCESS,                                    // null_literal
        result = Result::SUCCESS,                                    // none_literal
        result = Result::SUCCESS,                                    // undefined_literal
        result = analyze_array_literal(*inner, expr),                // array_literal
        result = Result::SUCCESS,                                    // string_literal
        result = analyze_struct_literal(*inner),                     // struct_literal
        result = analyze_closure_literal(*inner, expr),              // closure_literal
        SIR_VISIT_IGNORE,                                            // symbol_expr
        result = analyze_binary_expr(*inner, expr),                  // binary_expr
        result = analyze_unary_expr(*inner, expr),                   // unary_expr
        analyze_cast_expr(*inner),                                   // cast_expr
        SIR_VISIT_IGNORE,                                            // index_expr
        result = analyze_call_expr(*inner),                          // call_expr
        SIR_VISIT_IGNORE,                                            // field_expr
        analyze_range_expr(*inner),                                  // range_expr
        analyze_tuple_expr(*inner),                                  // tuple_expr
        SIR_VISIT_IGNORE,                                            // primitive_type
        SIR_VISIT_IGNORE,                                            // pointer_type
        result = analyze_static_array_type(*inner),                  // static_array_type
        analyze_func_type(*inner),                                   // func_type
        result = analyze_optional_type(*inner, expr),                // optional_type
        result = analyze_array_type(*inner, expr),                   // array_type
        result = analyze_closure_type(*inner),                       // closure_type
        result = analyze_ident_expr(*inner, expr),                   // ident_expr
        analyze_star_expr(*inner, expr),                             // star_expr
        result = analyze_bracket_expr(*inner, expr),                 // bracket_expr
        result = analyze_dot_expr(*inner, expr),                     // dot_expr
        result = Result::SUCCESS,                                    // meta_access
        result = MetaExprEvaluator(analyzer).evaluate(*inner, expr), // meta_field_expr
        result = MetaExprEvaluator(analyzer).evaluate(*inner, expr)  // meta_call_expr
    );

    if (result != Result::SUCCESS) {
        return result;
    }

    if (auto type_alias = expr.match_symbol<sir::TypeAlias>()) {
        expr = type_alias->type;
    }

    return Result::SUCCESS;
}

Result ExprAnalyzer::finalize_type_by_coercion(sir::Expr &expr, sir::Expr &expected_type) {
    Result result;

    if (expr.get_type() != expected_type) {
        if (auto specialization = as_std_optional_specialization(expected_type)) {
            if (auto none_literal = expr.match<sir::NoneLiteral>()) {
                create_std_optional_none(*specialization, expr);
                return Result::SUCCESS;
            }

            result = finalize_type_by_coercion(expr, specialization->args[0]);
            if (result != Result::SUCCESS) {
                return result;
            }

            create_std_optional_some(*specialization, expr);
            return Result::SUCCESS;
        }
    }

    if (auto array_literal = expr.match<sir::ArrayLiteral>()) {
        if (auto specialization = as_std_array_specialization(expected_type)) {
            sir::Expr element_type = specialization->args[0];

            array_literal->type = expected_type;
            finalize_array_literal_elements(*array_literal, element_type);
            create_std_array(*array_literal, element_type, expr);
        } else if (auto static_array_type = expected_type.match<sir::StaticArrayType>()) {
            array_literal->type = expected_type;
            finalize_array_literal_elements(*array_literal, static_array_type->base_type);

            unsigned length = array_literal->values.size();
            unsigned expected_length = static_array_type->length.as<sir::IntLiteral>().value.to_u64();

            if (length != expected_length) {
                analyzer.report_generator.report_err_unexpected_array_length(*array_literal, expected_length);
                return Result::ERROR;
            }
        } else {
            analyzer.report_generator.report_err_cannot_coerce(*array_literal, expected_type);
            return Result::ERROR;
        }
    } else if (auto struct_literal = expr.match<sir::StructLiteral>()) {
        if (!struct_literal->type) {
            if (expected_type.is_symbol<sir::StructDef>()) {
                struct_literal->type = expected_type;
            } else {
                analyzer.report_generator.report_err_cannot_coerce(*struct_literal, expected_type);
                return Result::ERROR;
            }
        }

        finalize_struct_literal_fields(*struct_literal);
    }

    if (expr.get_type() == expected_type) {
        return Result::SUCCESS;
    }

    if (auto int_literal = expr.match<sir::IntLiteral>()) {
        if (expected_type.is_int_type()) {
            int_literal->type = expected_type;
            return Result::SUCCESS;
        } else {
            analyzer.report_generator.report_err_cannot_coerce(*int_literal, expected_type);
            return Result::ERROR;
        }
    } else if (auto fp_literal = expr.match<sir::FPLiteral>()) {
        if (expected_type.is_fp_type()) {
            fp_literal->type = expected_type;
            return Result::SUCCESS;
        } else {
            analyzer.report_generator.report_err_cannot_coerce(*fp_literal, expected_type);
            return Result::ERROR;
        }
    } else if (auto null_literal = expr.match<sir::NullLiteral>()) {
        if (expected_type.is_addr_like_type()) {
            null_literal->type = expected_type;
            return Result::SUCCESS;
        } else {
            analyzer.report_generator.report_err_cannot_coerce(*null_literal, expected_type);
            return Result::ERROR;
        }
    } else if (auto none_literal = expr.match<sir::NoneLiteral>()) {
        analyzer.report_generator.report_err_cannot_coerce(*none_literal, expected_type);
        return Result::ERROR;
    } else if (auto undefined_literal = expr.match<sir::UndefinedLiteral>()) {
        undefined_literal->type = expected_type;
        return Result::SUCCESS;
    } else if (auto string_literal = expr.match<sir::StringLiteral>()) {
        if (expected_type.is_u8_ptr()) {
            string_literal->type = expected_type;
            return Result::SUCCESS;
        } else if (expected_type.is_symbol(analyzer.find_std_string())) {
            create_std_string(*string_literal, expr);
            return Result::SUCCESS;
        } else {
            analyzer.report_generator.report_err_cannot_coerce(*string_literal, expected_type);
            return Result::ERROR;
        }
    }

    analyzer.report_generator.report_err_type_mismatch(expr, expected_type, expr.get_type());
    return Result::ERROR;
}

Result ExprAnalyzer::finalize_type(sir::Expr &expr) {
    if (auto int_literal = expr.match<sir::IntLiteral>()) {
        int_literal->type = analyzer.create_expr(sir::PrimitiveType{
            .ast_node = nullptr,
            .primitive = sir::Primitive::I32,
        });
    } else if (auto fp_literal = expr.match<sir::FPLiteral>()) {
        fp_literal->type = analyzer.create_expr(sir::PrimitiveType{
            .ast_node = nullptr,
            .primitive = sir::Primitive::F32,
        });
    } else if (auto null_literal = expr.match<sir::NullLiteral>()) {
        null_literal->type = analyzer.create_expr(sir::PrimitiveType{
            .ast_node = nullptr,
            .primitive = sir::Primitive::ADDR,
        });
    } else if (auto none_literal = expr.match<sir::NoneLiteral>()) {
        analyzer.report_generator.report_err_cannot_infer_type(*none_literal);
        return Result::ERROR;
    } else if (auto undefined_literal = expr.match<sir::UndefinedLiteral>()) {
        analyzer.report_generator.report_err_cannot_infer_type(*undefined_literal);
        return Result::ERROR;
    } else if (auto array_literal = expr.match<sir::ArrayLiteral>()) {
        if (!array_literal->values.empty()) {
            finalize_array_literal_elements(*array_literal, nullptr);
            create_std_array(*array_literal, array_literal->values[0].get_type(), expr);
            return Result::SUCCESS;
        } else {
            analyzer.report_generator.report_err_cannot_infer_type(*array_literal);
            return Result::ERROR;
        }
    } else if (auto string_literal = expr.match<sir::StringLiteral>()) {
        create_std_string(*string_literal, expr);
        return Result::SUCCESS;
    } else if (auto struct_literal = expr.match<sir::StructLiteral>()) {
        if (struct_literal->type) {
            finalize_struct_literal_fields(*struct_literal);
        } else {
            analyzer.report_generator.report_err_cannot_infer_type(*struct_literal);
            return Result::ERROR;
        }
    }

    return Result::SUCCESS;
}

void ExprAnalyzer::analyze_bool_literal(sir::BoolLiteral &bool_literal) {
    bool_literal.type = analyzer.create_expr(sir::PrimitiveType{
        .ast_node = nullptr,
        .primitive = sir::Primitive::BOOL,
    });
}

void ExprAnalyzer::analyze_char_literal(sir::CharLiteral &char_literal) {
    char_literal.type = analyzer.create_expr(sir::PrimitiveType{
        .ast_node = nullptr,
        .primitive = sir::Primitive::U8,
    });
}

Result ExprAnalyzer::analyze_array_literal(sir::ArrayLiteral &array_literal, sir::Expr &out_expr) {
    Result result = Result::SUCCESS;
    Result partial_result;

    for (sir::Expr &value : array_literal.values) {
        partial_result = ExprAnalyzer(analyzer).analyze(value);
        if (partial_result != Result::SUCCESS) {
            result = Result::ERROR;
        }
    }

    if (result == Result::SUCCESS && array_literal.values.size() == 1 && array_literal.values[0].is_type()) {
        out_expr = analyzer.create_expr(sir::ArrayType{
            .ast_node = array_literal.ast_node,
            .base_type = array_literal.values[0],
        });

        return analyze_array_type(out_expr.as<sir::ArrayType>(), out_expr);
    }

    return result;
}

Result ExprAnalyzer::analyze_struct_literal(sir::StructLiteral &struct_literal) {
    Result result;

    if (struct_literal.type) {
        result = ExprAnalyzer(analyzer).analyze(struct_literal.type);
        if (result != Result::SUCCESS) {
            return result;
        }
    }

    for (sir::StructLiteralEntry &entry : struct_literal.entries) {
        result = ExprAnalyzer(analyzer).analyze_uncoerced(entry.value);
        if (result != Result::SUCCESS) {
            return result;
        }
    }

    return Result::SUCCESS;
}

Result ExprAnalyzer::analyze_closure_literal(sir::ClosureLiteral &closure_literal, sir::Expr &out_expr) {
    sir::TupleExpr *data_type = analyzer.create_expr(sir::TupleExpr{
        .ast_node = nullptr,
        .type = nullptr,
        .exprs = {},
    });

    sir::Param data_ptr_param{
        .ast_node = nullptr,
        .name = {},
        .type = analyzer.create_expr(sir::PrimitiveType{
            .ast_node = nullptr,
            .primitive = sir::Primitive::ADDR,
        }),
    };

    sir::FuncType generated_func_type = closure_literal.func_type;
    generated_func_type.params.insert(generated_func_type.params.begin(), data_ptr_param);

    sir::FuncDef *generated_func = analyzer.create_decl(sir::FuncDef{
        .ast_node = nullptr,
        .ident{
            .ast_node = nullptr,
            .value = "",
        },
        .type = generated_func_type,
        .block = closure_literal.block,
        .attrs = nullptr,
        .generic_params = {},
        .specializations = {},
    });

    generated_func->block.symbol_table->parent = analyzer.get_scope().decl_block->symbol_table;

    ClosureContext closure_ctx{
        .captured_vars = {},
        .data_type = data_type,
        .parent_block = analyzer.get_scope().block,
    };

    analyzer.push_scope().closure_ctx = &closure_ctx;

    DeclInterfaceAnalyzer(analyzer).analyze_func_def(*generated_func);
    DeclBodyAnalyzer(analyzer).analyze_func_def(*generated_func);

    analyzer.pop_scope();

    analyzer.cur_sir_mod->block.decls.push_back(generated_func);

    data_type->exprs.resize(closure_ctx.captured_vars.size());
    std::vector<sir::Expr> capture_values(closure_ctx.captured_vars.size());

    for (unsigned i = 0; i < closure_ctx.captured_vars.size(); i++) {
        sir::Symbol &captured_var = closure_ctx.captured_vars[i];

        sir::Expr type = captured_var.get_type();
        sir::Expr value = analyzer.create_expr(sir::SymbolExpr{
            .ast_node = nullptr,
            .type = type,
            .symbol = captured_var,
        });

        data_type->exprs[i] = type;
        capture_values[i] = value;
    }

    sir::TupleExpr *data = analyzer.create_expr(sir::TupleExpr{
        .ast_node = nullptr,
        .type = data_type,
        .exprs = capture_values,
    });

    sir::StructDef &std_closure_def = analyzer.find_std_closure().as<sir::StructDef>();

    sir::FuncDef &new_def_generic = std_closure_def.block.symbol_table->look_up("new").as<sir::FuncDef>();
    sir::FuncDef &new_def = *GenericsSpecializer(analyzer).specialize(new_def_generic, {data->type});

    sir::Expr callee = analyzer.create_expr(sir::SymbolExpr{
        .ast_node = nullptr,
        .type = &new_def.type,
        .symbol = &new_def,
    });

    sir::Expr addr_type = analyzer.create_expr(sir::PrimitiveType{
        .ast_node = nullptr,
        .primitive = sir::Primitive::ADDR,
    });

    sir::Expr func_ptr = analyzer.create_expr(sir::CastExpr{
        .ast_node = nullptr,
        .type = addr_type,
        .value = analyzer.create_expr(sir::SymbolExpr{
            .ast_node = nullptr,
            .type = &generated_func->type,
            .symbol = generated_func,
        }),
    });

    out_expr = analyzer.create_expr(sir::CallExpr{
        .ast_node = nullptr,
        .type = analyzer.create_expr(sir::ClosureType{
            .ast_node = nullptr,
            .func_type = closure_literal.func_type,
            .underlying_struct = &std_closure_def,
        }),
        .callee = callee,
        .args = {func_ptr, data},
    });

    return Result::SUCCESS;
}

Result ExprAnalyzer::analyze_binary_expr(sir::BinaryExpr &binary_expr, sir::Expr &out_expr) {
    ExprAnalyzer(analyzer).analyze(binary_expr.lhs);
    ExprAnalyzer(analyzer).analyze(binary_expr.rhs);

    if (auto struct_def = binary_expr.lhs.get_type().match_symbol<sir::StructDef>()) {
        std::string_view impl_name = MagicMethods::look_up(binary_expr.op);
        sir::Symbol symbol = struct_def->block.symbol_table->look_up(impl_name);

        if (!symbol) {
            analyzer.report_generator.report_err_operator_overload_not_found(binary_expr);
            return Result::ERROR;
        }

        return analyze_operator_overload_call(symbol, binary_expr.lhs, binary_expr.rhs, out_expr);
    }

    switch (binary_expr.op) {
        case sir::BinaryOp::ADD:
        case sir::BinaryOp::SUB:
        case sir::BinaryOp::MUL:
        case sir::BinaryOp::DIV:
        case sir::BinaryOp::MOD:
        case sir::BinaryOp::BIT_AND:
        case sir::BinaryOp::BIT_OR:
        case sir::BinaryOp::BIT_XOR:
        case sir::BinaryOp::SHL:
        case sir::BinaryOp::SHR: binary_expr.type = binary_expr.lhs.get_type(); break;
        case sir::BinaryOp::EQ:
        case sir::BinaryOp::NE:
        case sir::BinaryOp::GT:
        case sir::BinaryOp::LT:
        case sir::BinaryOp::GE:
        case sir::BinaryOp::LE:
        case sir::BinaryOp::AND:
        case sir::BinaryOp::OR:
            binary_expr.type = analyzer.create_expr(sir::PrimitiveType{
                .ast_node = nullptr,
                .primitive = sir::Primitive::BOOL,
            });
            break;
    }

    return Result::SUCCESS;
}

Result ExprAnalyzer::analyze_unary_expr(sir::UnaryExpr &unary_expr, sir::Expr &out_expr) {
    Result partial_result;

    // Deref unary operations are handled by `analyze_star_expr`.
    ASSUME(unary_expr.op != sir::UnaryOp::DEREF);

    partial_result = ExprAnalyzer(analyzer).analyze(unary_expr.value);
    if (partial_result != Result::SUCCESS) {
        return Result::ERROR;
    }

    if (unary_expr.op == sir::UnaryOp::REF) {
        unary_expr.type = analyzer.create_expr(sir::PointerType{
            .ast_node = nullptr,
            .base_type = unary_expr.value.get_type(),
        });

        return Result::SUCCESS;
    }

    if (auto struct_def = unary_expr.value.get_type().match_symbol<sir::StructDef>()) {
        std::string_view impl_name = MagicMethods::look_up(unary_expr.op);
        sir::Symbol symbol = struct_def->block.symbol_table->look_up(impl_name);

        if (!symbol) {
            analyzer.report_generator.report_err_operator_overload_not_found(unary_expr);
            return Result::ERROR;
        }

        return analyze_operator_overload_call(symbol, unary_expr.value, nullptr, out_expr);
    }

    if (unary_expr.op == sir::UnaryOp::NEG) {
        unary_expr.type = unary_expr.value.get_type();
    } else if (unary_expr.op == sir::UnaryOp::NOT) {
        unary_expr.type = analyzer.create_expr(sir::PrimitiveType{
            .ast_node = nullptr,
            .primitive = sir::Primitive::BOOL,
        });
    } else {
        ASSERT_UNREACHABLE;
    }

    return Result::SUCCESS;
}

void ExprAnalyzer::analyze_cast_expr(sir::CastExpr &cast_expr) {
    ExprAnalyzer(analyzer).analyze(cast_expr.type);
    ExprAnalyzer(analyzer).analyze(cast_expr.value);
}

Result ExprAnalyzer::analyze_call_expr(sir::CallExpr &call_expr) {
    Result result;

    if (auto dot_expr = call_expr.callee.match<sir::DotExpr>()) {
        result = analyze_dot_expr_callee(*dot_expr, call_expr);
    } else {
        result = ExprAnalyzer(analyzer).analyze(call_expr.callee);
    }

    if (result != Result::SUCCESS) {
        return result;
    }

    for (unsigned i = 0; i < call_expr.args.size(); i++) {
        sir::Expr &arg = call_expr.args[i];

        ExprConstraints constraints;

        if (auto func_def = call_expr.callee.match_symbol<sir::FuncDef>()) {
            if (!func_def->is_generic()) {
                constraints = ExprConstraints::expect_type(func_def->type.params[i].type);
            }
        } else if (auto native_func_decl = call_expr.callee.match_symbol<sir::NativeFuncDecl>()) {
            constraints = ExprConstraints::expect_type(native_func_decl->type.params[i].type);
        }

        result = ExprAnalyzer(analyzer, constraints).analyze(arg);
        if (result != Result::SUCCESS) {
            return result;
        }
    }

    if (auto func_def = call_expr.callee.match_symbol<sir::FuncDef>()) {
        if (func_def->is_generic()) {
            auto generic_args = GenericArgDeduction(analyzer).deduce(func_def->generic_params, call_expr.args);
            if (!generic_args) {
                return result;
            }

            result = specialize(*func_def, *generic_args, call_expr.callee);
            if (result != Result::SUCCESS) {
                return result;
            }
        }
    } else if (auto overload_set = call_expr.callee.match_symbol<sir::OverloadSet>()) {
        sir::FuncDef *func_def = resolve_overload(*overload_set, call_expr.args);

        call_expr.callee = analyzer.create_expr(sir::SymbolExpr{
            .ast_node = nullptr,
            .type = &func_def->type,
            .symbol = func_def,
        });
    }

    sir::Expr callee_type = call_expr.callee.get_type();

    if (auto func_type = callee_type.match<sir::FuncType>()) {
        call_expr.type = callee_type.as<sir::FuncType>().return_type;
    } else if (auto closure_type = callee_type.match<sir::ClosureType>()) {
        call_expr.type = closure_type->func_type.return_type;

        sir::Expr func_ptr = analyzer.create_expr(sir::FieldExpr{
            .ast_node = nullptr,
            .type = closure_type->underlying_struct->fields[0]->type,
            .base = call_expr.callee,
            .field_index = 0,
        });

        sir::Expr data_ptr = analyzer.create_expr(sir::FieldExpr{
            .ast_node = nullptr,
            .type = closure_type->underlying_struct->fields[1]->type,
            .base = call_expr.callee,
            .field_index = 1,
        });

        call_expr.callee = analyzer.create_expr(sir::CastExpr{
            .ast_node = nullptr,
            .type = &closure_type->func_type,
            .value = func_ptr,
        });

        call_expr.args.insert(call_expr.args.begin(), data_ptr);
    } else {
        analyzer.report_generator.report_err_cannot_call(call_expr.callee);
        return Result::ERROR;
    }

    return Result::SUCCESS;
}

Result ExprAnalyzer::analyze_dot_expr_callee(sir::DotExpr &dot_expr, sir::CallExpr &out_call_expr) {
    Result result;

    result = ExprAnalyzer(analyzer).analyze(dot_expr.lhs);
    if (result != Result::SUCCESS) {
        return result;
    }

    if (auto struct_def = dot_expr.lhs.get_type().match_symbol<sir::StructDef>()) {
        sir::Symbol method = struct_def->block.symbol_table->look_up(dot_expr.rhs.value);

        out_call_expr.callee = analyzer.create_expr(sir::SymbolExpr{
            .ast_node = dot_expr.rhs.ast_node,
            .type = method.get_type(),
            .symbol = method,
        });

        sir::Expr self_arg = analyzer.create_expr(sir::UnaryExpr{
            .ast_node = nullptr,
            .type = analyzer.create_expr(sir::PointerType{
                .ast_node = nullptr,
                .base_type = dot_expr.lhs.get_type(),
            }),
            .op = sir::UnaryOp::REF,
            .value = dot_expr.lhs,
        });

        out_call_expr.args.insert(out_call_expr.args.begin(), self_arg);
    } else if (auto pointer_type = dot_expr.lhs.get_type().match<sir::PointerType>()) {
        if (auto struct_def = pointer_type->base_type.match_symbol<sir::StructDef>()) {
            sir::Symbol method = struct_def->block.symbol_table->look_up(dot_expr.rhs.value);

            out_call_expr.callee = analyzer.create_expr(sir::SymbolExpr{
                .ast_node = dot_expr.rhs.ast_node,
                .type = method.get_type(),
                .symbol = method,
            });

            out_call_expr.args.insert(out_call_expr.args.begin(), dot_expr.lhs);
        } else {
            result = analyze_dot_expr_rhs(dot_expr, out_call_expr.callee);
            return result;
        }
    } else {
        result = analyze_dot_expr_rhs(dot_expr, out_call_expr.callee);
        return result;
    }

    return Result::SUCCESS;
}

Result ExprAnalyzer::analyze_static_array_type(sir::StaticArrayType &static_array_type) {
    Result result = Result::SUCCESS;
    Result partial_result;

    partial_result = ExprAnalyzer(analyzer).analyze(static_array_type.base_type);
    if (partial_result != Result::SUCCESS) {
        result = Result::ERROR;
    }

    partial_result = ExprAnalyzer(analyzer).analyze(static_array_type.length);
    if (partial_result != Result::SUCCESS) {
        result = Result::ERROR;
    }

    static_array_type.length = ConstEvaluator(analyzer).evaluate(static_array_type.length);

    return result;
}

void ExprAnalyzer::analyze_func_type(sir::FuncType &func_type) {
    for (sir::Param &param : func_type.params) {
        ExprAnalyzer(analyzer).analyze(param.type);
    }

    ExprAnalyzer(analyzer).analyze(func_type.return_type);
}

Result ExprAnalyzer::analyze_optional_type(sir::OptionalType &optional_type, sir::Expr &out_expr) {
    Result partial_result;

    partial_result = ExprAnalyzer(analyzer).analyze(optional_type.base_type);
    if (partial_result != Result::SUCCESS) {
        return Result::ERROR;
    }

    sir::StructDef &struct_def = analyzer.find_std_optional().as<sir::StructDef>();
    specialize(struct_def, {optional_type.base_type}, out_expr);

    return Result::SUCCESS;
}

Result ExprAnalyzer::analyze_array_type(sir::ArrayType &array_type, sir::Expr &out_expr) {
    Result partial_result;

    partial_result = ExprAnalyzer(analyzer).analyze(array_type.base_type);
    if (partial_result != Result::SUCCESS) {
        return Result::ERROR;
    }

    sir::StructDef &struct_def = analyzer.find_std_array().as<sir::StructDef>();
    specialize(struct_def, {array_type.base_type}, out_expr);

    return Result::SUCCESS;
}

Result ExprAnalyzer::analyze_closure_type(sir::ClosureType &closure_type) {
    sir::StructDef &std_closure_def = analyzer.find_std_closure().as<sir::StructDef>();
    closure_type.underlying_struct = &std_closure_def;
    return Result::SUCCESS;
}

Result ExprAnalyzer::analyze_dot_expr(sir::DotExpr &dot_expr, sir::Expr &out_expr) {
    Result result = ExprAnalyzer(analyzer).analyze(dot_expr.lhs);
    if (result != Result::SUCCESS) {
        return result;
    }

    result = analyze_dot_expr_rhs(dot_expr, out_expr);
    return result;
}

void ExprAnalyzer::analyze_range_expr(sir::RangeExpr &range_expr) {
    ExprAnalyzer(analyzer).analyze(range_expr.lhs);
    ExprAnalyzer(analyzer).analyze(range_expr.rhs);
}

void ExprAnalyzer::analyze_tuple_expr(sir::TupleExpr &tuple_expr) {
    assert(!tuple_expr.exprs.empty());

    for (sir::Expr &expr : tuple_expr.exprs) {
        ExprAnalyzer(analyzer).analyze(expr);
    }

    if (tuple_expr.exprs[0].is_type()) {
        return;
    }

    std::vector<sir::Expr> types;
    types.resize(tuple_expr.exprs.size());

    for (unsigned i = 0; i < tuple_expr.exprs.size(); i++) {
        types[i] = tuple_expr.exprs[i].get_type();
    }

    tuple_expr.type = analyzer.create_expr(sir::TupleExpr{
        .ast_node = nullptr,
        .type = nullptr,
        .exprs = types,
    });
}

Result ExprAnalyzer::analyze_ident_expr(sir::IdentExpr &ident_expr, sir::Expr &out_expr) {
    sir::SymbolTable &symbol_table = analyzer.get_symbol_table();
    const std::unordered_map<std::string_view, sir::Expr> &generic_args = analyzer.get_scope().generic_args;
    ClosureContext *closure_ctx = analyzer.get_scope().closure_ctx;

    if (!generic_args.empty()) {
        auto iter = generic_args.find(ident_expr.value);
        if (iter != generic_args.end()) {
            out_expr = iter->second;
            return Result::SUCCESS;
        }
    }

    if (analyzer.in_meta_expansion) {
        UseResolver(analyzer).resolve_in_block(*analyzer.get_scope().decl_block);
    }

    sir::Symbol symbol = symbol_table.look_up(ident_expr.value);

    if (!symbol && closure_ctx) {
        symbol = closure_ctx->parent_block->symbol_table->look_up(ident_expr.value);

        if (!symbol) {
            analyzer.report_generator.report_err_symbol_not_found(ident_expr);
            return Result::ERROR;
        }

        unsigned captured_var_index = 0;
        bool captured_var_exists = false;

        for (unsigned i = 0; i < closure_ctx->captured_vars.size(); i++) {
            if (closure_ctx->captured_vars[i] == symbol) {
                captured_var_index = i;
                captured_var_exists = true;
                break;
            }
        }

        if (!captured_var_exists) {
            captured_var_index = closure_ctx->captured_vars.size();
            closure_ctx->captured_vars.push_back(symbol);
        }

        sir::Symbol data_ptr_param = &analyzer.get_scope().func_def->type.params[0];

        out_expr = analyzer.create_expr(sir::FieldExpr{
            .ast_node = nullptr,
            .type = symbol.get_type(),
            .base = analyzer.create_expr(sir::UnaryExpr{
                .ast_node = nullptr,
                .type = closure_ctx->data_type,
                .op = sir::UnaryOp::DEREF,
                .value = analyzer.create_expr(sir::CastExpr{
                    .ast_node = nullptr,
                    .type = analyzer.create_expr(sir::PointerType{
                        .ast_node = nullptr,
                        .base_type = closure_ctx->data_type,
                    }),
                    .value = analyzer.create_expr(sir::SymbolExpr{
                        .ast_node = nullptr,
                        .type = data_ptr_param.get_type(),
                        .symbol = data_ptr_param,
                    })
                }),
            }),
            .field_index = captured_var_index,
        });

        return Result::SUCCESS;
    }

    if (!symbol) {
        analyzer.report_generator.report_err_symbol_not_found(ident_expr);
        return Result::ERROR;
    }

    out_expr = analyzer.create_expr(sir::SymbolExpr{
        .ast_node = ident_expr.ast_node,
        .type = symbol.get_type(),
        .symbol = symbol,
    });

    return Result::SUCCESS;
}

Result ExprAnalyzer::analyze_star_expr(sir::StarExpr &star_expr, sir::Expr &out_expr) {
    Result result;

    result = ExprAnalyzer(analyzer).analyze(star_expr.value);
    if (result != Result::SUCCESS) {
        return result;
    }

    if (star_expr.value.is_type()) {
        out_expr = analyzer.create_expr(sir::PointerType{
            .ast_node = star_expr.ast_node,
            .base_type = star_expr.value,
        });
    } else {
        if (auto struct_def = star_expr.value.get_type().match_symbol<sir::StructDef>()) {
            std::string_view impl_name = MagicMethods::look_up(sir::UnaryOp::DEREF);
            sir::Symbol symbol = struct_def->block.symbol_table->look_up(impl_name);

            if (!symbol) {
                analyzer.report_generator.report_err_operator_overload_not_found(star_expr);
                return Result::ERROR;
            }

            return analyze_operator_overload_call(symbol, star_expr.value, nullptr, out_expr);
        }

        out_expr = analyzer.create_expr(sir::UnaryExpr{
            .ast_node = star_expr.ast_node,
            .type = star_expr.value.get_type().as<sir::PointerType>().base_type,
            .op = sir::UnaryOp::DEREF,
            .value = star_expr.value,
        });
    }

    return Result::SUCCESS;
}

Result ExprAnalyzer::analyze_bracket_expr(sir::BracketExpr &bracket_expr, sir::Expr &out_expr) {
    Result result;

    result = ExprAnalyzer(analyzer).analyze(bracket_expr.lhs);
    if (result != Result::SUCCESS) {
        return result;
    }

    for (sir::Expr &expr : bracket_expr.rhs) {
        ExprAnalyzer(analyzer).analyze(expr);
    }

    if (auto func_def = bracket_expr.lhs.match_symbol<sir::FuncDef>()) {
        if (func_def->is_generic()) {
            return specialize(*func_def, bracket_expr.rhs, out_expr);
        }
    } else if (auto struct_def = bracket_expr.lhs.match_symbol<sir::StructDef>()) {
        if (struct_def->is_generic()) {
            return specialize(*struct_def, bracket_expr.rhs, out_expr);
        }
    }

    const sir::Expr &lhs_type = bracket_expr.lhs.get_type();

    if (auto pointer_type = lhs_type.match<sir::PointerType>()) {
        out_expr = analyzer.create_expr(sir::IndexExpr{
            .ast_node = bracket_expr.ast_node,
            .type = pointer_type->base_type,
            .base = bracket_expr.lhs,
            .index = bracket_expr.rhs[0],
        });
    } else if (auto static_array_type = lhs_type.match<sir::StaticArrayType>()) {
        out_expr = analyzer.create_expr(sir::IndexExpr{
            .ast_node = bracket_expr.ast_node,
            .type = static_array_type->base_type,
            .base = bracket_expr.lhs,
            .index = bracket_expr.rhs[0],
        });
    } else if (auto struct_def = lhs_type.match_symbol<sir::StructDef>()) {
        // FIXME: error handling
        ASSUME(bracket_expr.rhs.size() == 1);

        sir::Symbol symbol = struct_def->block.symbol_table->look_up(MagicMethods::OP_INDEX);

        if (!symbol) {
            analyzer.report_generator.report_err_operator_overload_not_found(bracket_expr);
            return Result::ERROR;
        }

        result = analyze_operator_overload_call(symbol, bracket_expr.lhs, bracket_expr.rhs[0], out_expr);

        if (result == Result::SUCCESS) {
            out_expr = analyzer.create_expr(sir::UnaryExpr{
                .ast_node = nullptr,
                .type = out_expr.get_type().as<sir::PointerType>().base_type,
                .op = sir::UnaryOp::DEREF,
                .value = out_expr,
            });
        }

        return result;
    } else {
        // FIXME: error handling
        ASSERT_UNREACHABLE;
    }

    return Result::SUCCESS;
}

void ExprAnalyzer::create_std_string(sir::StringLiteral &string_literal, sir::Expr &out_expr) {
    sir::StructDef &struct_def = analyzer.find_std_string().as<sir::StructDef>();
    sir::FuncDef &func_def = struct_def.block.symbol_table->look_up("from_cstr").as<sir::FuncDef>();

    sir::Expr callee = analyzer.create_expr(sir::SymbolExpr{
        .ast_node = nullptr,
        .type = &func_def.type,
        .symbol = &func_def,
    });

    sir::Expr arg = analyzer.create_expr(sir::StringLiteral{
        .ast_node = string_literal.ast_node,
        .type = func_def.type.params[0].type,
        .value = std::move(string_literal.value),
    });

    out_expr = analyzer.create_expr(sir::CallExpr{
        .ast_node = nullptr,
        .type = func_def.type.return_type,
        .callee = callee,
        .args = {arg},
    });
}

sir::Specialization<sir::StructDef> *ExprAnalyzer::as_std_array_specialization(sir::Expr &type) {
    if (auto struct_def = type.match_symbol<sir::StructDef>()) {
        sir::StructDef &array_def = analyzer.find_std_array().as<sir::StructDef>();

        for (sir::Specialization<sir::StructDef> &specialization : array_def.specializations) {
            if (specialization.def == struct_def) {
                return &specialization;
            }
        }
    }

    return nullptr;
}

void ExprAnalyzer::create_std_array(
    sir::ArrayLiteral &array_literal,
    const sir::Expr &element_type,
    sir::Expr &out_expr
) {
    array_literal.type = analyzer.create_expr(sir::StaticArrayType{
        .ast_node = nullptr,
        .base_type = element_type,
        .length = analyzer.create_expr(sir::IntLiteral{
            .ast_node = nullptr,
            .type = nullptr,
            .value = static_cast<unsigned>(array_literal.values.size()),
        }),
    });

    sir::Expr array_pointer = analyzer.create_expr(sir::UnaryExpr{
        .ast_node = nullptr,
        .type = analyzer.create_expr(sir::PointerType{
            .ast_node = nullptr,
            .base_type = array_literal.type,
        }),
        .op = sir::UnaryOp::REF,
        .value = &array_literal,
    });

    sir::Expr data_pointer = analyzer.create_expr(sir::CastExpr{
        .ast_node = nullptr,
        .type = analyzer.create_expr(sir::PointerType{
            .ast_node = nullptr,
            .base_type = element_type,
        }),
        .value = array_pointer,
    });

    sir::Expr length = analyzer.create_expr(sir::IntLiteral{
        .ast_node = nullptr,
        .type = analyzer.create_expr(sir::PrimitiveType{
            .ast_node = nullptr,
            .primitive = sir::Primitive::USIZE,
        }),
        .value = static_cast<unsigned>(array_literal.values.size()),
    });

    sir::StructDef &array_type = analyzer.find_std_array().as<sir::StructDef>();
    sir::StructDef *specialization = GenericsSpecializer(analyzer).specialize(array_type, {element_type});
    sir::FuncDef &func_def = specialization->block.symbol_table->look_up("from").as<sir::FuncDef>();

    sir::Expr callee = analyzer.create_expr(sir::SymbolExpr{
        .ast_node = nullptr,
        .type = &func_def.type,
        .symbol = &func_def,
    });

    out_expr = analyzer.create_expr(sir::CallExpr{
        .ast_node = nullptr,
        .type = func_def.type.return_type,
        .callee = callee,
        .args = {data_pointer, length},
    });
}

sir::Specialization<sir::StructDef> *ExprAnalyzer::as_std_optional_specialization(sir::Expr &type) {
    if (auto struct_def = type.match_symbol<sir::StructDef>()) {
        sir::StructDef &optional_def = analyzer.find_std_optional().as<sir::StructDef>();

        for (sir::Specialization<sir::StructDef> &specialization : optional_def.specializations) {
            if (specialization.def == struct_def) {
                return &specialization;
            }
        }
    }

    return nullptr;
}

void ExprAnalyzer::create_std_optional_some(
    sir::Specialization<sir::StructDef> &specialization,
    sir::Expr &inout_expr
) {
    sir::FuncDef &func_def = specialization.def->block.symbol_table->look_up("new_some").as<sir::FuncDef>();

    sir::Expr callee = analyzer.create_expr(sir::SymbolExpr{
        .ast_node = nullptr,
        .type = &func_def.type,
        .symbol = &func_def,
    });

    inout_expr = analyzer.create_expr(sir::CallExpr{
        .ast_node = nullptr,
        .type = func_def.type.return_type,
        .callee = callee,
        .args = {inout_expr},
    });
}

void ExprAnalyzer::create_std_optional_none(sir::Specialization<sir::StructDef> &specialization, sir::Expr &out_expr) {
    sir::FuncDef &func_def = specialization.def->block.symbol_table->look_up("new_none").as<sir::FuncDef>();

    sir::Expr callee = analyzer.create_expr(sir::SymbolExpr{
        .ast_node = nullptr,
        .type = &func_def.type,
        .symbol = &func_def,
    });

    out_expr = analyzer.create_expr(sir::CallExpr{
        .ast_node = nullptr,
        .type = func_def.type.return_type,
        .callee = callee,
        .args = {},
    });
}

Result ExprAnalyzer::finalize_array_literal_elements(sir::ArrayLiteral &array_literal, sir::Expr element_type) {
    Result result = Result::SUCCESS;
    Result partial_result;

    for (sir::Expr &value : array_literal.values) {
        if (element_type) {
            partial_result = ExprAnalyzer(analyzer).finalize_type_by_coercion(value, element_type);
        } else {
            partial_result = ExprAnalyzer(analyzer).finalize_type(value);
            element_type = value.get_type();
        }

        if (partial_result != Result::SUCCESS) {
            result = Result::ERROR;
        }
    }

    return result;
}

Result ExprAnalyzer::finalize_struct_literal_fields(sir::StructLiteral &struct_literal) {
    Result result = Result::SUCCESS;
    Result partial_result;

    sir::StructDef &struct_def = struct_literal.type.as<sir::SymbolExpr>().symbol.as<sir::StructDef>();

    for (sir::StructLiteralEntry &entry : struct_literal.entries) {
        for (sir::StructField *field : struct_def.fields) {
            if (field->ident.value == entry.ident.value) {
                entry.field = field;
            }
        }

        if (!entry.field) {
            analyzer.report_generator.report_err_no_field(entry.ident, struct_def);
            result = Result::ERROR;
            continue;
        }

        partial_result = ExprAnalyzer(analyzer).finalize_type_by_coercion(entry.value, entry.field->type);
        if (partial_result != Result::SUCCESS) {
            result = Result::ERROR;
        }
    }

    return result;
}

Result ExprAnalyzer::analyze_dot_expr_rhs(sir::DotExpr &dot_expr, sir::Expr &out_expr) {
    sir::DeclBlock *decl_block = dot_expr.lhs.get_decl_block();
    if (decl_block) {
        sir::Symbol symbol = decl_block->symbol_table->look_up(dot_expr.rhs.value);
        if (!symbol) {
            analyzer.report_generator.report_err_symbol_not_found(dot_expr.rhs);
            return Result::ERROR;
        }

        out_expr = analyzer.create_expr(sir::SymbolExpr{
            .ast_node = dot_expr.ast_node,
            .type = symbol.get_type(),
            .symbol = symbol,
        });

        return Result::SUCCESS;
    }

    if (dot_expr.lhs.is_value()) {
        sir::Expr lhs_type = dot_expr.lhs.get_type();

        if (auto symbol_expr = lhs_type.match<sir::SymbolExpr>()) {
            sir::StructDef &struct_def = lhs_type.as<sir::SymbolExpr>().symbol.as<sir::StructDef>();
            sir::StructField *field = struct_def.find_field(dot_expr.rhs.value);

            out_expr = analyzer.create_expr(sir::FieldExpr{
                .ast_node = dot_expr.ast_node,
                .type = field->type,
                .base = dot_expr.lhs,
                .field_index = field->index,
            });
        } else if (auto pointer_expr = lhs_type.match<sir::PointerType>()) {
            sir::StructDef &struct_def = pointer_expr->base_type.as<sir::SymbolExpr>().symbol.as<sir::StructDef>();
            sir::StructField *field = struct_def.find_field(dot_expr.rhs.value);

            out_expr = analyzer.create_expr(sir::FieldExpr{
                .ast_node = dot_expr.ast_node,
                .type = field->type,
                .base = analyzer.create_expr(sir::UnaryExpr{
                    .ast_node = nullptr,
                    .type = pointer_expr->base_type,
                    .op = sir::UnaryOp::DEREF,
                    .value = dot_expr.lhs,
                }),
                .field_index = field->index,
            });
        } else if (auto tuple_expr = lhs_type.match<sir::TupleExpr>()) {
            unsigned field_index = std::stoul(dot_expr.rhs.value);

            out_expr = analyzer.create_expr(sir::FieldExpr{
                .ast_node = dot_expr.ast_node,
                .type = tuple_expr->exprs[field_index],
                .base = dot_expr.lhs,
                .field_index = field_index,
            });
        } else {
            ASSERT_UNREACHABLE;
        }
    } else {
        return Result::ERROR;
    }

    return Result::SUCCESS;
}

sir::FuncDef *ExprAnalyzer::resolve_overload(sir::OverloadSet &overload_set, const std::vector<sir::Expr> &args) {
    for (sir::FuncDef *func_def : overload_set.func_defs) {
        if (is_matching_overload(*func_def, args)) {
            return func_def;
        }
    }

    ASSERT_UNREACHABLE;
}

bool ExprAnalyzer::is_matching_overload(sir::FuncDef &func_def, const std::vector<sir::Expr> &args) {
    if (func_def.type.params.size() != args.size()) {
        return false;
    }

    for (unsigned i = 0; i < args.size(); i++) {
        if (args[i].get_type() != func_def.type.params[i].type) {
            return false;
        }
    }

    return true;
}

Result ExprAnalyzer::analyze_operator_overload_call(
    sir::Symbol symbol,
    sir::Expr self,
    sir::Expr arg,
    sir::Expr &out_expr
) {
    sir::Expr self_ref = analyzer.create_expr(sir::UnaryExpr{
        .ast_node = nullptr,
        .type = analyzer.create_expr(sir::PointerType{
            .ast_node = nullptr,
            .base_type = self.get_type(),
        }),
        .op = sir::UnaryOp::REF,
        .value = self,
    });

    std::vector<sir::Expr> args;
    if (arg) {
        args = {self_ref, arg};
    } else {
        args = {self_ref};
    }

    sir::FuncDef *impl;

    if (auto func_def = symbol.match<sir::FuncDef>()) {
        impl = func_def;
    } else if (auto overload_set = symbol.match<sir::OverloadSet>()) {
        impl = resolve_overload(*overload_set, args);
    } else {
        ASSERT_UNREACHABLE;
    }

    sir::Expr callee = analyzer.create_expr(sir::SymbolExpr{
        .ast_node = nullptr,
        .type = &impl->type,
        .symbol = impl,
    });

    out_expr = analyzer.create_expr(sir::CallExpr{
        .ast_node = nullptr,
        .type = impl->type.return_type,
        .callee = callee,
        .args = args,
    });

    return Result::SUCCESS;
}

Result ExprAnalyzer::specialize(
    sir::FuncDef &func_def,
    const std::vector<sir::Expr> &generic_args,
    sir::Expr &inout_expr
) {
    sir::FuncDef *specialization = GenericsSpecializer(analyzer).specialize(func_def, generic_args);

    inout_expr = analyzer.create_expr(sir::SymbolExpr{
        .ast_node = inout_expr.get_ast_node(),
        .type = &specialization->type,
        .symbol = specialization,
    });

    return Result::SUCCESS;
}

Result ExprAnalyzer::specialize(
    sir::StructDef &struct_def,
    const std::vector<sir::Expr> &generic_args,
    sir::Expr &inout_expr
) {
    sir::StructDef *specialization = GenericsSpecializer(analyzer).specialize(struct_def, generic_args);

    inout_expr = analyzer.create_expr(sir::SymbolExpr{
        .ast_node = inout_expr.get_ast_node(),
        .type = nullptr,
        .symbol = specialization,
    });

    return Result::SUCCESS;
}

} // namespace sema

} // namespace lang

} // namespace banjo
