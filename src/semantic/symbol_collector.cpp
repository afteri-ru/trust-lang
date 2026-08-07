#include "semantic/symbol_collector.hpp"

#include "semantic/pass.hpp"
#include "semantic/symbol_table.hpp"
#include "types/registry.hpp"
#include "types/type_id.hpp"

#include <set>

namespace trust {

SymbolCollectorHook::SymbolCollectorHook(AnalysisContext& actx)
: m_actx(actx) {
}

void SymbolCollectorHook::onDeclare(const Symbol& sym) {
    // ВАЖНО: SymbolTable::declareOrComplete ПЕРЕМЕЩАЕТ sym в таблицу (std::move(sym)),
    // поэтому здесь sym.name уже пуст (moved-from). Имя берём из узла объявления.
    if (!sym.decl) {
        return;
    }
    const std::string name(sym.decl->text());
    if (name.empty()) {
        return;
    }
    Entry e;
    e.name = name;
    e.type = sym.type;
    e.decl = sym.decl;
    if (const AstNodeBase* creator = m_actx.symbols().currentCreator()) {
        e.scopeRange = creator->range();
    }
    m_entries.push_back(std::move(e));
}

void SymbolCollectorHook::finalize() {
    auto& out = m_actx.symbolIndex();
    for (auto& e : m_entries) {
        SymbolInfo si;
        si.name = std::move(e.name);

        // Финальный тип: у VarDecl — выведенный (VarDecl::inferredType, post-order);
        // у прочих (FuncDecl/TypeDecl/ArgNode) — тип из Symbol::type.
        TypeId t = e.type;
        if (auto* vd = dynamic_cast<VarDecl*>(e.decl)) {
            if (vd->inferredType != INVALID_TYPE_ID) {
                t = vd->inferredType;
            }
        }
        si.type = clearInferred(t);
        si.typeName = m_actx.ctx().types().getFullTypeName(si.type);

        // Поля словаря/кортежа из инициализатора-литерала `x := (a=1, b=2,)` — для
        // member-завершения `x.`. Тип такого литерала — универсальный Dict (поля в нём
        // не хранятся), поэтому имена полей берём из узла DictLiteral (AssignOp m_left).
        if (auto* vd = dynamic_cast<VarDecl*>(e.decl)) {
            if (vd->m_initializer && vd->m_initializer->kind() == ParserToken::Kind::DictLiteral) {
                std::set<std::string> seen;
                const auto& body = static_cast<Sequence&>(*vd->m_initializer).m_body;
                for (const auto& el : body) {
                    if (!el || el->kind() != ParserToken::Kind::ArgNode) {
                        continue;
                    }
                    const std::string fname(static_cast<const ArgNode&>(*el).text());
                    if (!fname.empty() && seen.insert(fname).second) {
                        si.dictFields.push_back(fname);
                    }
                }
            }
        }

        // Диапазон имени: у VarDecl — точный (nameRange); у прочих — диапазон узла объявления.
        si.nameRange = e.decl->range();
        if (auto* vd = dynamic_cast<VarDecl*>(e.decl)) {
            const MapperRange nr = vd->nameRange();
            if (!nr.isInvalid()) {
                si.nameRange = nr;
            }
        }
        si.scopeRange = e.scopeRange;
        // Документирующий комментарий (`///`, `##`, `/**`, т.ч. хвостовой `///<`/`##<`),
        // привязанный грамматикой к терму-идентификатору и перенесённый в узел объявления
        // (TermToAstConverter::convert из term->m_docs). Для LSP hover/док.
        si.documentation = e.decl->documentation;
        out.push_back(std::move(si));
    }
    m_entries.clear();
}

} // namespace trust
