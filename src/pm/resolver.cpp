// src/pm/resolver.cpp
#include "pm/resolver.hpp"

#include "pm/protocol.hpp"
#include "pm/semver.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <vector>

// libsolv declares a struct field named `requires`, which is a C++20 keyword.
// Rename it for this translation unit only; the field is then accessed as
// `solvable->solv_requires`.
#define requires solv_requires
#include <solv/pool.h>
#include <solv/problems.h>
#include <solv/queue.h>
#include <solv/repo.h>
#include <solv/solver.h>
#include <solv/transaction.h>
#undef requires

namespace trust::pm {
namespace {

// libsolv defines REL_GT/REL_EQ/REL_LT but no combined REL_GE/REL_LE.
constexpr int kRelGe = REL_GT | REL_EQ;
constexpr int kRelLe = REL_LT | REL_EQ;

// A single libsolv relation: version <op> value.
struct RelClause {
    int op = REL_EQ;
    Semver version;
};

std::string_view trim(std::string_view text) {
    std::size_t b = 0;
    std::size_t e = text.size();
    while (b < e && std::isspace(static_cast<unsigned char>(text[b])) != 0) {
        ++b;
    }
    while (e > b && std::isspace(static_cast<unsigned char>(text[e - 1])) != 0) {
        --e;
    }
    return text.substr(b, e - b);
}

// Loose version parse: missing components default to zero.
Semver parseLoose(std::string_view text) {
    Semver v;
    int* values[3] = {&v.major, &v.minor, &v.patch};
    int index = 0;
    std::size_t pos = 0;
    while (pos <= text.size() && index < 3) {
        const std::size_t dot = text.find('.', pos);
        const std::string_view part = (dot == std::string_view::npos) ? text.substr(pos) : text.substr(pos, dot - pos);
        int value = 0;
        for (const char c : part) {
            if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
                throw PmException(
                    PmError{"pm.version.invalid", "invalid version component in range: " + std::string(text), nlohmann::json{{"range", std::string(text)}}});
            }
            value = value * 10 + (c - '0');
        }
        *values[index] = value;
        ++index;
        if (dot == std::string_view::npos) {
            break;
        }
        pos = dot + 1;
    }
    return v;
}

// Decomposes a range into conjunctions of libsolv relations. Empty result
// means "any version of the name".
std::vector<RelClause> rangeClauses(const std::string& range) {
    std::vector<RelClause> clauses;
    const std::string_view whole = trim(range);
    if (whole.empty() || whole == "*") {
        return clauses;
    }
    std::size_t pos = 0;
    while (pos <= whole.size()) {
        const std::size_t comma = whole.find(',', pos);
        std::string_view clause = (comma == std::string_view::npos) ? whole.substr(pos) : whole.substr(pos, comma - pos);
        clause = trim(clause);
        std::string_view op;
        for (const std::string_view candidate : {std::string_view(">="), std::string_view("<="), std::string_view(">"), std::string_view("<"),
                                                 std::string_view("="), std::string_view("^"), std::string_view("~")}) {
            if (clause.substr(0, candidate.size()) == candidate) {
                op = candidate;
                break;
            }
        }
        const Semver v = parseLoose(trim(clause.substr(op.size())));
        if (op == "^") {
            Semver upper;
            if (v.major > 0) {
                upper = Semver{v.major + 1, 0, 0};
            } else if (v.minor > 0) {
                upper = Semver{0, v.minor + 1, 0};
            } else {
                upper = Semver{0, 0, v.patch + 1};
            }
            clauses.push_back(RelClause{kRelGe, v});
            clauses.push_back(RelClause{REL_LT, upper});
        } else if (op == "~") {
            const bool two_components = clause.find('.', op.size() + 1) != std::string_view::npos;
            const Semver upper = two_components ? Semver{v.major, v.minor + 1, 0} : Semver{v.major + 1, 0, 0};
            clauses.push_back(RelClause{kRelGe, v});
            clauses.push_back(RelClause{REL_LT, upper});
        } else if (op == ">=") {
            clauses.push_back(RelClause{kRelGe, v});
        } else if (op == ">") {
            clauses.push_back(RelClause{REL_GT, v});
        } else if (op == "<=") {
            clauses.push_back(RelClause{kRelLe, v});
        } else if (op == "<") {
            clauses.push_back(RelClause{REL_LT, v});
        } else {
            clauses.push_back(RelClause{REL_EQ, v});
        }
        if (comma == std::string_view::npos) {
            break;
        }
        pos = comma + 1;
    }
    return clauses;
}

} // namespace

namespace {

// Appends the requires relations of `record` to its solvable.
void addRequires(Pool* pool, Repo* repo, Solvable* solvable, const std::vector<Dependency>& dependencies) {
    for (const Dependency& dep : dependencies) {
        const Id name = pool_str2id(pool, dep.name.c_str(), 1);
        const std::vector<RelClause> clauses = rangeClauses(dep.range);
        if (clauses.empty()) {
            const Id rel = pool_rel2id(pool, name, 0, 0, 1);
            solvable->solv_requires = repo_addid_dep(repo, solvable->solv_requires, rel, 0);
            continue;
        }
        for (const RelClause& clause : clauses) {
            const Id evr = pool_str2id(pool, formatSemver(clause.version).c_str(), 1);
            const Id rel = pool_rel2id(pool, name, evr, clause.op, 1);
            solvable->solv_requires = repo_addid_dep(repo, solvable->solv_requires, rel, 0);
        }
    }
}

} // namespace

std::vector<Resolved> resolveDependencies(const Manifest& root, const std::vector<PackageRecord>& candidates) {
    if (candidates.empty() && !root.dependencies.empty()) {
        throw PmException(PmError{"pm.resolve.conflict", "no candidate packages available from the configured sources", nlohmann::json{{"root", root.name}}});
    }

    Pool* pool = pool_create();
    pool_setdisttype(pool, DISTTYPE_RPM);
    Repo* repo = repo_create(pool, "candidates");

    std::map<Id, std::size_t> solvable_to_record;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const PackageRecord& record = candidates[i];
        const Id solvable_id = repo_add_solvable(repo);
        Solvable* solvable = pool_id2solvable(pool, solvable_id);
        solvable->name = pool_str2id(pool, record.name.c_str(), 1);
        solvable->evr = pool_str2id(pool, record.version.c_str(), 1);
        const Id provides = pool_rel2id(pool, solvable->name, solvable->evr, REL_EQ, 1);
        solvable->provides = repo_addid_dep(repo, solvable->provides, provides, 0);
        addRequires(pool, repo, solvable, record.dependencies);
        solvable_to_record[solvable_id] = i;
    }

    Queue job;
    queue_init(&job);
    for (const Dependency& dep : root.dependencies) {
        const Id name = pool_str2id(pool, dep.name.c_str(), 1);
        const std::vector<RelClause> clauses = rangeClauses(dep.range);
        if (clauses.empty()) {
            queue_push2(&job, SOLVER_INSTALL | SOLVER_SOLVABLE_NAME, name);
            continue;
        }
        for (const RelClause& clause : clauses) {
            const Id evr = pool_str2id(pool, formatSemver(clause.version).c_str(), 1);
            const Id rel = pool_rel2id(pool, name, evr, clause.op, 1);
            queue_push2(&job, SOLVER_INSTALL | SOLVER_SOLVABLE_PROVIDES, rel);
        }
    }

    pool_createwhatprovides(pool);
    Solver* solver = solver_create(pool);
    const int rc = solver_solve(solver, &job);
    if (rc != 0) {
        std::vector<std::string> problems;
        const unsigned int count = solver_problem_count(solver);
        for (unsigned int i = 1; i <= count; ++i) {
            const char* text = solver_problem2str(solver, i);
            if (text != nullptr) {
                problems.emplace_back(text);
            }
        }
        solver_free(solver);
        queue_free(&job);
        pool_free(pool);
        throw PmException(PmError{"pm.resolve.conflict", "dependency resolution failed", nlohmann::json{{"problems", problems}}});
    }

    std::vector<Resolved> result;
    Transaction* transaction = solver_create_transaction(solver);
    for (int i = 0; i < transaction->steps.count; ++i) {
        const Id p = transaction->steps.elements[i];
        const Id type = transaction_type(transaction, p, SOLVER_TRANSACTION_SHOW_ALL);
        if (type != SOLVER_TRANSACTION_INSTALL) {
            continue;
        }
        const auto it = solvable_to_record.find(p);
        if (it == solvable_to_record.end()) {
            continue;
        }
        const PackageRecord& record = candidates[it->second];
        result.push_back(Resolved{record.name, record.version, record.file, record.url, record.sha256, record.source});
    }
    transaction_free(transaction);
    solver_free(solver);
    queue_free(&job);
    pool_free(pool);

    std::sort(result.begin(), result.end(), [](const Resolved& a, const Resolved& b) { return a.name < b.name; });
    return result;
}

} // namespace trust::pm
