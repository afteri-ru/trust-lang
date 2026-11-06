// Назначение `DeclId` объявлениям дерева (R23): единственный именованный путь — `assignDeclarationIds`.
// Идемпотентно: id определяется позицией объявления (pre-order) в модуле-владельце, а не счётчиком сессии.
#pragma once

#include "ast/token_base.hpp"
#include "location/decl_id.hpp"

#include <string_view>
#include <vector>

namespace trust {

class DeclCppFacts;

/// Присвоить `DeclId` каждому объявлению (`VarDecl`/`FuncDecl`/`ClassDecl`/`RecordDecl`) дерева в
/// детерминированном pre-order порядке. `moduleLogicalName` — логическое имя модуля-владельца (стабильно
/// между прогонами). Повторный вызов на том же дереве даёт те же id (идемпотентность); порядок загрузки
/// независимых модулей на id не влияет.
/// `declFacts` (необязательный sink) — целевые C++-факты объявлений: временный носитель
/// `Decl::m_pendingNativeCppName` (заполнен конвертером Term→AST до появления id) переносится под ключ
/// `DeclId` и очищается; узлового ключа (адреса) нет — переиспользование адреса дало бы ложную привязку (R1).
void assignDeclarationIds(const std::vector<AstNodePtr>& roots, std::string_view moduleLogicalName, DeclCppFacts* declFacts = nullptr);

/// Идентичность объявления узла (R23): `DeclId` носителя-объявления (`VarDecl`/`FuncDecl`/
/// `ClassDecl`/`RecordDecl`) либо `TypeDecl`-обёртки, когда она сама является объявлением (alias —
/// без объявления-ребёнка); у типа с телом id принадлежит объявлению-ребёнку. `DeclId{}`
/// (невалидный) — узел объявлением не является либо id не назначен (синтетика).
/// Адресная/текстовая идентичность не используется.
[[nodiscard]] DeclId declarationIdOf(const AstNodeBase& node) noexcept;

} // namespace trust
