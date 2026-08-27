#include "syntax/term.h"

using namespace trust;

ArgsPair& Term::push_back(const ArgsPair& p) {
    if (!m_args) {
        m_args.emplace();
    }
    m_args->push_back(p);
    return m_args->back();
}

ArgsPair& Term::push_back(const TermPtr value, const std::string& name) {
    return push_back(ArgsPair(name, value));
}

TermPtr Term::Create(TermID id, std::string text, trust::MapperRange mapperRange, parser::token_type lex_type) {
    return std::make_shared<Term>(id, std::move(text), mapperRange, lex_type);
}

TermPtr Term::Create(TermID id, parser::token_type lex_type, const char* text, size_t len, trust::MapperRange mapperRange) {
    return std::make_shared<Term>(id, std::string_view(text, len), mapperRange, lex_type);
}

TermID Term::symbolToID(char sym) {
    switch (sym) {
#define SYM_CASE_CHAR(name, ch) \
    case ch:                    \
        return TermID::name;
#define SYM_CASE_NONE(name) /* маркер без символа - пропускаем */
#define SYM_SELECT(_1, _2, NAME, ...) NAME
#define SYM_GEN(...) SYM_SELECT(__VA_ARGS__, SYM_CASE_CHAR, SYM_CASE_NONE)(__VA_ARGS__)
        SYMBOL_TOKENS(SYM_GEN)
#undef SYM_GEN
#undef SYM_SELECT
#undef SYM_CASE_NONE
#undef SYM_CASE_CHAR
    default:
        FAULT("Term::symbolToID: unknown symbol '{}'", sym);
    }
}

parser::token_type Term::tokenFromID(TermID id) {
    switch (id) {
#define TK_CASE_CHAR(name, ch) \
    case TermID::name:         \
        return parser::token_type::name;
#define TK_CASE_NONE(name) /* маркер без символа - пропускаем */
#define TK_SELECT(_1, _2, NAME, ...) NAME
#define TK_GEN(...) TK_SELECT(__VA_ARGS__, TK_CASE_CHAR, TK_CASE_NONE)(__VA_ARGS__)
        SYMBOL_TOKENS(TK_GEN)
#undef TK_GEN
#undef TK_SELECT
#undef TK_CASE_NONE
#undef TK_CASE_CHAR
    default:
        FAULT("Term::tokenFromID: unknown symbol '{}'", trust::toString(id));
    }
}

TermPtr Term::CreateSymbol(char sym) {
    return Create(symbolToID(sym), std::string(1, sym), {}, tokenFromID(symbolToID(sym)));
}

TermPtr Term::Clone() {
    TermPtr result = Term::Create(m_id, std::string(getText()), {}, m_lexer_type);
    *result.get() = *this;
    return result;
}

Term::Term(TermID id, std::string text, trust::MapperRange mapperRange, parser::token_type lex_type) {
    m_lexer_type = lex_type;
    m_mapperRange = mapperRange;
    m_text = std::move(text);
    m_id = id;
}

Term::Term(TermID id, std::string_view text, trust::MapperRange mapperRange, parser::token_type lex_type) {
    m_lexer_type = lex_type;
    m_mapperRange = mapperRange;
    m_text = text;
    m_id = id;
}

void Term::appendParenItems_(std::string& str) const {
    str += "(";
    dump_items_(str);
    str += ")";
}

void Term::appendBracketItems_(std::string& str) const {
    str += "[";
    dump_items_(str);
    str += "]";
}

void Term::appendSemicolon_(std::string& str, bool nested) const {
    if (!nested) {
        str += ";";
    }
}

void Term::appendBlockItems_(std::string& str, bool nested) {
    for (size_t i = 0; i < m_sequence.size(); i++) {
        if (i) {
            str += " ";
        }
        str += m_sequence[i]->toString(true);
        if (!str.empty() && str[str.size() - 1] != ';') {
            str += ";";
        }
    }
}

void Term::dump_items_(std::string& str) const {
    if (!m_args) {
        return;
    }
    bool first = true;
    for (auto elem : *m_args) {
        if (first) {
            first = false;
        } else {
            str.append(", ");
        }
        if (elem.second->getTermID() == TermID::ARGUMENT && elem.second->m_left && elem.second->m_right) {
            // Именованный аргумент: имя:Тип=значение - тип берём из m_type (ЕДИНЫЙ слот,
            // нормализация грамматики), у значения тип подавляем (иначе задублируется: имя:Int32=1:Int32)
            str.append(elem.first);
            if (elem.second->GetType()) {
                str += elem.second->GetType()->toString(true, true);
            }
            str.append("=");
            str.append(elem.second->m_right->toString(true, true));
        } else {
            if (!elem.first.empty()) {
                str.append(elem.first);
                if (elem.second->GetType()) {
                    str += elem.second->GetType()->toString(true, true);
                }
                str.append("=");
            }
            str.append(elem.second->toString(true));
        }
    }
}

std::string Term::toString(bool nested, bool suppressType) {
    std::string result;
    if (m_left) {
        if (!result.empty()) {
            result += "=";
        }
        ASSERT(this != m_left.get());
        result += m_left->toString();
    }

    TermPtr temp;

    switch (m_id) {
    case TermID::END:
        result += "<END>";
        return result;

    case TermID::FIELD:
        // Доступ по имени/статическому индексу: m_left.ключ (объект-как-корень в новой
        // грамматике). Для поля без объекта - просто текст (как раньше).
        if (m_left) {
            result = m_left->toString() + "." + (m_right ? m_right->toString() : "");
            return result;
        }
        return getText();

    case TermID::MACRO_STR:
        result = "@@@";
        result += getText();
        result += "@@@@";
        return result;

    case TermID::ARGS:
    case TermID::ITERATOR:
        result += getText();
        if (size()) {
            appendParenItems_(result);
        }
        return result;

    case TermID::ARGUMENT:
        // Именованный аргумент: m_left=имя, m_type=тип, m_right=значение (аналог Python ast.keyword)
        if (m_left && m_right) {
            std::string argName = std::string(m_left->getText());
            result = argName;
            if (GetType()) {
                result += GetType()->toString(true, true);
            }
            result += "=";
            result += m_right->toString(true, true);
            return result;
        }
        // Лексема $123 - позиционный аргумент, как раньше
        result += getText();
        if (size()) {
            appendParenItems_(result);
        }
        return result;

    case TermID::INT_PLUS:
    case TermID::INT_MINUS: {
        // Роль (return/throw) определяется по TermID, а не по тексту.
        // m_text единообразно хранит namespace (лексема '++'/'--' заменена в exit).
        const char* op = m_id == TermID::INT_PLUS ? "++" : "--";
        std::string_view ns = getText();
        if (!ns.empty() && ns != op) {
            result = std::string(ns) + " " + op;
        } else {
            result = op;
        }
        if (m_right) {
            result += " ";
            result += m_right->toString();
        }
        return result;
    }

    case TermID::INDEX:
        result = "";
        if (m_left) {
            result += m_left->toString();
        }
        if (size()) {
            appendBracketItems_(result); // "[a,b,...]" из m_args (все индексы)
        } else if (m_right) {
            result += "[" + m_right->toString() + "]";
        }
        return result;

    case TermID::TAKE:
    case TermID::OPERATOR_PTR:
        // Префиксный оператор (&, *) над типом: ':&Int8', ':*Int8'
        if (m_right && m_right->getTermID() == TermID::TYPE) {
            result = std::string(":") + std::string(getText());
            result += m_right->toString(true).substr(1);
            return result;
        }
        // Префиксный оператор: операнд печатается всегда, даже при nested=true.
        result = getText();
        if (m_right) {
            result += m_right->toString(true);
        }
        if (size()) {
            appendParenItems_(result);
        } else if (m_args.has_value()) {
            result += "()";
        }
        return result;

    case TermID::NONE:
    case TermID::PARENT:
    case TermID::MODULE:
    case TermID::TRUSTLANG:
    case TermID::NATIVE:
    case TermID::MANGLED:
    case TermID::MACRO:
    case TermID::LOCAL:
    case TermID::STATIC:
    case TermID::WITH:
    case TermID::NAME:
        result = "";
        temp = shared_from_this();
        if (temp->m_left) {
            result = temp->m_left->toString();
        }
        while (!nested && temp->m_right) {
            if (this == temp->m_right.get()) {
                ASSERT(this != temp->m_right.get());
            }
            if (temp->m_right->m_left) {
                if (this == temp->m_right->m_left.get()) {
                    break;
                }
                ASSERT(this != temp->m_right->m_left.get());
            }
            result += temp->m_right->toString(true);
            temp = temp->m_right;
        }
        result.insert(0, getText());
        if (size()) {
            appendParenItems_(result);
        } else if (m_args.has_value()) {
            result += "()";
        }
        if (!suppressType && GetType()) {
            result += GetType()->toString(true, true);
        }
        return result;

    case TermID::STRCHAR:
    case TermID::STRWIDE:
        result = m_id == TermID::STRWIDE ? "\"" : "'";
        result += getText();
        result += m_id == TermID::STRWIDE ? "\"" : "'";
        if (size()) {
            appendParenItems_(result);
        }
        return result;

    case TermID::REFLECTION:
        result = "`";
        result += getText();
        result += "`";
        return result;

    case TermID::INTEGER:
    case TermID::NUMBER:
        result = getText();
        if (GetType() && !suppressType) {
            result += GetType()->toString(true, true);
        }
        return result;

    case TermID::ASSIGN:
    case TermID::CREATE_TYPE:
    case TermID::CREATE_NAME:
        if (m_id == TermID::ASSIGN) {
            result += getText();
        } else {
            result += " " + getText() + " ";
        }
        if (m_right) {
            // Цепочка элементов без m_left - это список (например, using_list), выводим с запятыми
            if (m_right->m_right && !m_right->m_left) {
                TermPtr cur = m_right;
                while (cur) {
                    if (cur != m_right) {
                        result += ",";
                    }
                    result += cur->toString(true);
                    cur = cur->m_right;
                }
            } else {
                result += m_right->toString();
            }
        }
        appendSemicolon_(result, nested);
        return result;

    case TermID::APPEND:
        result = m_left->toString();
        result += " " + getText() + " ";
        result += m_right->toString();
        appendSemicolon_(result, nested);
        return result;

    case TermID::RANGE:
        ASSERT(size() == 2 || size() == 3);
        result = at(0).second->toString() + ".." + at(1).second->toString();
        if (size() == 3) {
            result += ".." + at(2).second->toString();
        }
        return result;

    case TermID::FUNCTION:
        result += " " + getText() + " ";
        if (m_right && this != m_right.get()) {
            result += m_right->toString(true);
            if (!result.empty() && result[result.size() - 1] != ';') {
                result += ";";
            }
            for (int i = 0; i < m_right->size(); i++) {
                result += m_right->at(i).second->toString();
            }
        }
        return result;

    case TermID::TENSOR:
        result += "[";
        dump_items_(result);
        result += ",";
        result += "]";
        if (GetType()) {
            result += GetType()->toString(true, true);
        }
        return result;

    case TermID::DICT:
        result += "(";
        dump_items_(result);
        result += ",";
        result += ")";
        if (GetType()) {
            result += GetType()->toString(true, true);
        }
        return result;

    case TermID::TYPEDUCK:
    case TermID::TYPECAST:
    case TermID::TYPE:
        if (m_id == TermID::TYPEDUCK) {
            result += ":~~" + getText().substr(1);
        } else if (m_id == TermID::TYPECAST) {
            result += ":~" + getText().substr(1);
        } else {
            result += ":";
            result += getText().substr(1);
        }
        if (m_type && m_type->size()) {
            // На TYPE-узлах m_type хранит ARGS-терм размерностей [...]
            result += "[";
            for (int i = 0; i < m_type->size(); i++) {
                if (i) {
                    result += ",";
                }
                result += m_type->at(i).second->toString();
            }
            result += "]";
        }
        if (isCall()) {
            appendParenItems_(result);
        }
        if (m_sequence.size()) {
            result += "{";
            appendBlockItems_(result, nested);
            result += "}";
        }
        return result;

    case TermID::EMBED:
        result += "{%" + getText() + "%}";
        if (m_right) {
            result += m_right->toString();
        }
        return result;

    case TermID::WHILE:
        // Единая раскладка: m_left=cond, m_sequence=[body], m_right=else.
        result = "[" + result + "]" + getText();
        ASSERT(!m_sequence.empty() && m_sequence[0]);
        result += m_sequence[0]->toString() + ";";
        if (m_right) {
            result += ", [...]-->";
            result += m_right->toString();
            if (!(m_right->isBlock() || m_right->getTermID() == TermID::EMBED)) {
                result += ";";
            }
        }
        return result;

    case TermID::DOWHILE:
        // Единая раскладка: m_left=cond, m_sequence=[body].
        ASSERT(!m_sequence.empty() && m_sequence[0]);
        result = m_sequence[0]->toString() + getText() + "[";
        result += m_left->toString() + "];";
        return result;

    case TermID::FOLLOW:
        // Единая раскладка: m_left=cond, m_sequence=[thenBody, elseif-branch...], m_right=else.
        result.clear();
        if (m_left) {
            result += "[" + m_left->toString() + "]";
        } else {
            result += " ";
        }
        if (!m_sequence.empty() && m_sequence[0]) {
            result += "-->" + m_sequence[0]->toString() + ";";
        }
        for (size_t i = 1; i < m_sequence.size(); i++) {
            if (!m_sequence[i]) {
                continue;
            }
            result += ",\n ";
            if (m_sequence[i]->m_left) {
                result += "[" + m_sequence[i]->m_left->toString() + "]";
            } else {
                result += " ";
            }
            ASSERT(m_sequence[i]->m_right);
            result += "-->" + m_sequence[i]->m_right->toString() + ";";
        }
        if (m_right) {
            result += ",\n [...]-->";
            result += m_right->toString() + ";";
        }
        return result;

    case TermID::SEQUENCE:
    case TermID::BLOCK:
    case TermID::BLOCK_TRY:
    case TermID::BLOCK_PLUS:
    case TermID::BLOCK_MINUS:
        result = "";
        // Метка/namespace блока единообразно хранится в m_text (лексема '{' заменена).
        if (!getText().empty() && getText()[0] != '{' && getText()[0] != '$') {
            result += getText() + " ";
        }
        if (m_id == TermID::SEQUENCE) {
        } else if (m_id == TermID::BLOCK) {
            result += "{";
        } else if (m_id == TermID::BLOCK_TRY) {
            result += "{*";
        } else if (m_id == TermID::BLOCK_PLUS) {
            result += "{+";
        } else if (m_id == TermID::BLOCK_MINUS) {
            result += "{-";
        } else {
            FAULT("Unknown block type {} ({})", trust::toString(m_id), static_cast<uint8_t>(m_id));
        }
        appendBlockItems_(result, nested);
        if (m_id == TermID::SEQUENCE) {
        } else if (m_id == TermID::BLOCK) {
            result += "}";
        } else if (m_id == TermID::BLOCK_TRY) {
            result += "*}";
        } else if (m_id == TermID::BLOCK_PLUS) {
            result += "+}";
        } else if (m_id == TermID::BLOCK_MINUS) {
            result += "-}";
        } else {
            FAULT("Unknown block type {} ({})", trust::toString(m_id), static_cast<int>(m_id));
        }
        return result;

    case TermID::OP_MATH:
    case TermID::OP_BITWISE:
    case TermID::OP_COMPARE:
    case TermID::OP_LOGICAL:
        result += " " + getText() + " ";
        if (m_right) {
            result += m_right->toString();
        }
        return result;

    case TermID::ELLIPSIS:
        if (m_left) {
            result = m_left->toString() + " ";
        }
        result += getText();
        if (m_right) {
            result += m_right->toString();
        }
        return result;

    case TermID::FILLING:
        result += "..." + (m_right ? m_right->toString() : "") + "...";
        return result;

    case TermID::MACRO_DEL:
    case TermID::MACRO_SEQ:
        result = getText() + " ";
        for (size_t i = 0; i < m_sequence.size(); i++) {
            if (i) {
                result += " ";
            }
            if (m_sequence[i]->getTermID() == TermID::NAME) {
                result += m_sequence[i]->toString();
            } else {
                result += m_sequence[i]->getText();
            }
        }
        result += " " + getText();
        return result;

    case TermID::NAMESPACE:
    case TermID::LPAREN:
    case TermID::RPAREN:
    case TermID::LBRACKET:
    case TermID::RBRACKET:
    case TermID::SEMICOLON:
    case TermID::COMMA:
    case TermID::COMMA_LEXEME:
    case TermID::TRUST_ELEM_BEGIN:
    case TermID::TRUST_ELEM_END:
    case TermID::DOT:
    case TermID::COLON:
    case TermID::EQ:
    case TermID::PLUS:
    case TermID::MINUS:
    case TermID::STAR:
    case TermID::SLASH:
    case TermID::PERCENT:
    case TermID::AMP:
    case TermID::PIPE:
    case TermID::CARET:
    case TermID::TILDE:
    case TermID::BANG:
    case TermID::QUESTION:
    case TermID::AT:
    case TermID::DOLLAR:
    case TermID::LT:
    case TermID::GT:
    case TermID::RATIONAL:
    case TermID::COMPLEX:
    case TermID::MACRO_ARGCOUNT:
    case TermID::MACRO_ARGUMENT:
    case TermID::MACRO_ARGNAME:
    case TermID::MACRO_ARGPOS:
    case TermID::MACRO_TOSTR:
    case TermID::MACRO_CONTEXT:
        return getText();

    case TermID::ESCAPE:
        result = "@\\" + getText();
        return result;

    case TermID::CLASS:
        result = getText();
        if (isCall()) {
            appendParenItems_(result);
        }
        result += "{";
        appendBlockItems_(result, nested);
        result += "}";
        return result;

    default:
        throw ParserError("Fail toString() type %s, text:'%s'", trust::toString(m_id), std::string(getText()).c_str());
    }
}

void Term::AppendLeft(TermPtr item) {
    TermPtr next = shared_from_this();
    while (next->m_left) {
        ASSERT(next != next->m_left);
        next = next->m_left;
    }
    ASSERT(next != item);
    next->m_left = item;
}

void Term::AppendRight(TermPtr item) {
    TermPtr next = shared_from_this();
    if (next == item) {
    }
    while (next->m_right) {
        ASSERT(next != next->m_right);
        next = next->m_right;
    }
    next->m_right = item;
}

void Term::AppendText(const std::string& s) {
    getText().append(s);
}

void Term::RightToBlock(SequenceType& vect, bool remove) {
    TermPtr next = shared_from_this();
    TermPtr prev;
    vect.clear();
    while (next) {
        if (next->getTermID() != TermID::END) {
            vect.push_back(next);
        }
        prev = next;
        next = next->m_right;
        if (remove) {
            prev->m_right.reset();
        }
    }
}

TermPtr Term::AppendBlock(const TermPtr& item, TermID id, bool force) {
    if (force) {
        ASSERT(isBlock() && m_sequence.empty() && m_id == id);
        m_sequence.push_back(item);
        return shared_from_this();
    }
    TermPtr result;
    if (m_id == id || m_id == TermID::SEQUENCE) {
        if (m_id != id) {
            m_id = id;
        }
        result = shared_from_this();
        // Блок (item->isBlock()) добавляется как дочерний узел, а НЕ сплющивается:
        // иначе граница области видимости `{ ... }` теряется и объявления «протекают»
        // в охватывающий скоуп. Раньше здесь children блока вставлялись в результат.
        if (this != item.get()) {
            result->m_sequence.push_back(item);
        }
    } else {
        // this - не SEQUENCE (может быть и BLOCK и обычный statement): оборачиваем [this, item]
        // в новую SEQUENCE. Блок как первый элемент последовательности (за ним идёт ещё оператор)
        // - корректная конструкция, поэтому assert(!isBlock()) не ставим.
        result = Term::Create(id, "", item->m_mapperRange);
        result->m_sequence.push_back(shared_from_this());
        if (this != item.get()) {
            result->m_sequence.push_back(item);
        }
        if (item->m_id == TermID::SEQUENCE) {
            item->m_id = id;
        }
    }
    return result;
}

TermPtr Term::Last() {
    return m_right ? m_right->Last() : shared_from_this();
}

void Term::FinalizeAndTest(TermID id) {
    if (m_left) {
        std::string saved(getText());
        getText().clear();
        TermPtr cur = m_left;
        while (cur) {
            ASSERT(this != cur.get());
            getText().insert(0, cur->getText());
            cur = cur->m_left;
        }
        getText() += saved;
        cur = m_left;
        m_left.reset();
        while (cur) {
            ASSERT(this != cur.get());
            TermPtr prev = cur->m_left;
            cur->m_left.reset();
            cur = prev;
        }
    }
    m_id = id;
}

ArgsPair& Term::at(const int64_t index) {
    if (!m_args) {
        FAULT("Index '{}' not exists!", index);
    }
    if (index < 0) {
        if (-index <= static_cast<int64_t>(m_args->size())) {
            int64_t pos = index + 1;
            auto iter = m_args->end();
            while (iter != m_args->begin()) {
                iter--;
                if (pos == 0) {
                    return *iter;
                }
                pos++;
            }
        }
    } else {
        int64_t pos = 0;
        for (auto& elem : *m_args) {
            if (pos == index) {
                return elem;
            }
            pos++;
        }
    }
    FAULT("Index '{}' not exists!", index);
}
