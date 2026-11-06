#include "semantic/core/symbol_collector.hpp"

#include "semantic/core/pass.hpp"
#include "symbols/symbol_table.hpp"
#include "types/type_system.hpp"
#include "types/type_id.hpp"

#include <set>

namespace trust {

SymbolCollectorHook::SymbolCollectorHook(AnalysisContext& actx)
: m_actx(actx) {
}

bool SymbolCollectorHook::isEnabled() const {
    return true;
}

void SymbolCollectorHook::onDeclare(const Symbol& sym) {
    // sym НЕ moved-from (declareOrComplete копирует): имя доступно и в sym.name, и в узле.
    // Для диапазона объявления читаем узел декларации.
    if (!sym.decl) {
        return;
    }
    // `sym.decl` у тип-объявлений (`:T ::= ...`) — контейнер (Binary) без текста: имена типов
    // LSP берёт из TypeSystem, поэтому такой decl в SymbolIndex не попадает.
    const HasText* textNode = sym.decl->as_text();
    if (textNode == nullptr) {
        return;
    }
    const std::string_view name = textNode->text();
    if (name.empty()) {
        return;
    }
    Entry e;
    e.name = name;
    e.namespacePath = stableSymbolName(m_actx.namespaces().namespacePath());
    e.type = sym.type.getTypeId();
    e.decl = sym.decl;
    e.module = sym.decl->module.isValid() ? std::optional<ModuleIndex>(sym.decl->module) : std::nullopt;
    // Видимость объявления в ЕГО модуле: экспортируются только top-level объявления
    // не-private области. Site-зависимый import-hidden здесь не вычисляется (это проекция сайта):
    // источник — та же таблица видимости (`declarationStorageKey`), что и у регистрации.
    const bool moduleLevel = m_actx.namespaces().currentFunc() == nullptr && !m_actx.namespaces().insideTypeBody();
    const VisibilityClass declaredClass = m_actx.symbols().declarationStorageKey(name).visibility;
    e.visibility = (moduleLevel && !isFilePrivateVisibility(declaredClass)) ? VisibilityClass::Exported : VisibilityClass::FilePrivate;
    if (const AstNodeBase* creator = m_actx.symbols().currentCreator()) {
        e.scopeRange = creator->range();
    }
    m_entries.push_back(std::move(e));
}

void SymbolCollectorHook::finalize() {
    auto& out = m_actx.state().symbolIndex();
    for (auto& e : m_entries) {
        SymbolInfo si;
        si.name = e.name;
        si.namespacePath = e.namespacePath;

        // Финальный тип: у VarDecl - выведенный (VarDecl::inferredType, post-order);
        // у прочих (FuncDecl/TypeDecl/ArgNode) - тип из Symbol::type.
        TypeId t = e.type;
        if (e.decl && e.decl->is<VarDecl>()) {
            auto* vd = e.decl->as<VarDecl>();
            if (vd->inferredType.getTypeId() != INVALID_TYPE_ID) {
                t = vd->inferredType.getTypeId();
            }
        }
        si.type = t;
        si.typeName = m_actx.ctx().readTypes().printer().typeNameOf(t);

        // Поля словаря/кортежа из инициализатора-литерала `x := (a=1, b=2,)` - для
        // member-завершения `x.`. Тип такого литерала - универсальный Dict (поля в нём
        // не хранятся), поэтому имена полей берём из узла DictLiteral (AssignOp m_left).
        if (e.decl && e.decl->is<VarDecl>()) {
            auto* vd = e.decl->as<VarDecl>();
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

        // Диапазон имени: у VarDecl - точный (nameRange); у прочих - диапазон узла объявления.
        si.nameRange = e.decl->range();
        if (e.decl && e.decl->is<VarDecl>()) {
            auto* vd = e.decl->as<VarDecl>();
            const MapperRange nr = vd->nameRange();
            if (!nr.isInvalid()) {
                si.nameRange = nr;
            }
        }
        si.scopeRange = e.scopeRange;
        si.module = e.module;
        si.visibility = e.visibility;
        // Документирующий комментарий (`///`, `##`, `/**`, т.ч. хвостовой `///<`/`##<`),
        // привязанный грамматикой к терму-идентификатору и перенесённый в узел объявления
        // (TermToAstConverter::convert из term->m_docs). Для LSP hover/док.
        si.documentation = e.decl->documentation;
        out.push_back(std::move(si));
    }
    m_entries.clear();
}

} // namespace trust
