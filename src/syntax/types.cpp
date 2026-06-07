#include "syntax/types.h"
#include "syntax/term.h"

using namespace trust;

#define MAKE_TYPE(type_name) {type_name, Term::Create(TermID::TYPE, type_name)}

static const std::map<const std::string, const TermPtr> default_types{MAKE_TYPE(":None"),

                                                                      MAKE_TYPE(":Bool"),       MAKE_TYPE(":Int8"),      MAKE_TYPE(":Int16"),
                                                                      MAKE_TYPE(":Int32"),      MAKE_TYPE(":Int64"),

                                                                      MAKE_TYPE(":Float16"),    MAKE_TYPE(":Float32"),   MAKE_TYPE(":Float64"),

                                                                      MAKE_TYPE(":Complex16"),  MAKE_TYPE(":Complex32"), MAKE_TYPE(":Complex64"),

                                                                      MAKE_TYPE(":Rational"),

                                                                      MAKE_TYPE(":StrChar"),    MAKE_TYPE(":StrWide"),

                                                                      MAKE_TYPE(":Range"),      MAKE_TYPE(":Iterator"),

                                                                      MAKE_TYPE(":Dictionary"),

                                                                      MAKE_TYPE(":Any")};

#undef MAKE_TYPE

static const TermPtr type_default_none = default_types.find(":None")->second;
static const TermPtr type_default_any = default_types.find(":Any")->second;
static const TermPtr term_none = Term::Create(TermID::NAME, "_", {}, parser::token_type::NAME);
static const TermPtr term_ellipsys = Term::Create(TermID::ELLIPSIS, "...", {}, parser::token_type::ELLIPSIS);
static const TermPtr term_required = Term::Create(TermID::NONE, "_", {}, parser::token_type::END);

const TermPtr trust::getNoneTerm() {
    return term_none;
}

const TermPtr trust::getRequiredTerm() {
    return term_required;
}

const TermPtr trust::getEllipsysTerm() {
    return term_ellipsys;
}

void trust::NewLangSignalHandler(int signal) {
    fprintf(stderr, "Signal %d received\n", signal);
    std::abort();
}