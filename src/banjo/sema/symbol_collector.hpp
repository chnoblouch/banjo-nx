#ifndef SYMBOL_COLLECTOR_H
#define SYMBOL_COLLECTOR_H

#include "banjo/sema/semantic_analyzer.hpp"
#include "banjo/sir/sir.hpp"

namespace banjo {

namespace lang {

namespace sema {

class SymbolCollector {

private:
    SemanticAnalyzer &analyzer;

public:
    SymbolCollector(SemanticAnalyzer &analyzer);
    void collect(const std::vector<sir::Module *> &mods);
    void collect_in_block(sir::DeclBlock &decl_block);
    void collect_in_meta_block(sir::MetaBlock &meta_block);
    void collect_decl(sir::Decl &decl);

    void collect_func_def(sir::FuncDef &func_def);
    void collect_func_decl(sir::FuncDecl &func_decl);
    void collect_native_func_decl(sir::NativeFuncDecl &native_func_decl);
    void collect_const_def(sir::ConstDef &const_def);
    void collect_struct_def(sir::StructDef &struct_def);
    void collect_var_decl(sir::VarDecl &var_decl);
    void collect_native_var_decl(sir::NativeVarDecl &native_var_decl);
    void collect_enum_def(sir::EnumDef &enum_def);
    void collect_enum_variant(sir::EnumVariant &enum_variant);
    void collect_union_def(sir::UnionDef &union_def);
    void collect_union_case(sir::UnionCase &union_case);
    void collect_proto_def(sir::ProtoDef &proto_def);
    void collect_type_alias(sir::TypeAlias &type_alias);
    void collect_use_decl(sir::UseDecl &use_decl);

    void collect_use_item(sir::UseItem &use_item);
    void collect_use_ident(sir::UseIdent &use_ident);
    void collect_use_rebind(sir::UseRebind &use_rebind);
    void collect_use_dot_expr(sir::UseDotExpr &use_dot_expr);
    void collect_use_list(sir::UseList &use_list);

private:
    void add_symbol(std::string_view name, sir::Symbol symbol);
    sir::SymbolTable &get_symbol_table();
    void collect_in_decl(sir::Symbol decl, sir::DeclBlock &block);
};

} // namespace sema

} // namespace lang

} // namespace banjo

#endif
