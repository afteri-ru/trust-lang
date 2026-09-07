#include "pipeline/pipeline.hpp"
#include "diag/context.hpp"
#include "diag/diag.hpp"
#include "types/registry.hpp"
#include "utils/io.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>
#include <string_view>

using namespace trust;

namespace {

// Real-API тесты вывода типа нетипизированной `x := _;` и definite-assignment по ветвлениям.
// Компилируют trust-исходник через полный Pipeline (Parser → Semantic), захватывая диагностики.
// Файл намеренно минимизирует самописный AST: реальный синтаксис `:= _`/`= _`/`@match` покрывает
// слой пер-Symbol флагов и монотонный вывод типа (не std::any) без ручных узлов.
class UninitInferTest : public ::testing::Test {
  protected:
    void SetUp() override {
        m_stream.str("");
        m_prev_err = setErrs(&m_stream);
        m_types = std::make_unique<TypeRegistry>(m_ctx.diag(), m_ctx.opts());
        m_ctx.setTypes(m_types.get());
    }

    void TearDown() override { setErrs(m_prev_err); }

    // Запускает ParseAST + Semantic над исходником; возвращает число ошибок.
    int runSemantic(std::string_view src) {
        m_ctx.diag().clear();
        MapperFile sf = m_ctx.source().add_source("test.src", std::string(src), true);
        PipelineOpts opts;
        Pipeline pipeline(m_ctx, opts);
        PipelineResult result = pipeline.runPipeline(PipelineSteps::ParseAST | PipelineSteps::Semantic, sf);
        (void)result;
        return m_ctx.diag().errorCount();
    }

    std::string msgs() const { return m_stream.str(); }

    std::ostream* m_prev_err = nullptr;
    std::stringstream m_stream;
    Context m_ctx;
    std::unique_ptr<TypeRegistry> m_types;
};

// `x := _; x = 5; print(x)` - тип выводится по записи (монотонно), ошибок нет.
TEST_F(UninitInferTest, UntypedUnderscoreTypedByWrite_Ok) {
    const std::string src = "@main() := {\n"
                            "    x := _;\n"
                            "    x = 5;\n"
                            "    @print('{}', x);\n"
                            "};\n";
    EXPECT_EQ(runSemantic(src), 0) << msgs();
}

// Чтение до записи → Error (нет silent-UB / тихого std::any).
TEST_F(UninitInferTest, ReadBeforeWrite_Error) {
    const std::string src = "@main() := {\n"
                            "    x := _;\n"
                            "    @print('{}', x);\n"
                            "};\n";
    EXPECT_GT(runSemantic(src), 0);
    EXPECT_NE(msgs().find("read before it is initialized"), std::string::npos) << msgs();
}

// Сброс `x = _;` возвращает в неинициализированное состояние → чтение Error.
TEST_F(UninitInferTest, ResetThenRead_Error) {
    const std::string src = "@main() := {\n"
                            "    x := 5;\n"
                            "    x = _;\n"
                            "    @print('{}', x);\n"
                            "};\n";
    EXPECT_GT(runSemantic(src), 0);
    EXPECT_NE(msgs().find("read before it is initialized"), std::string::npos) << msgs();
}

// `x := _;` без записей и без использований → Error «cannot infer type» (НЕ -Wunused).
TEST_F(UninitInferTest, NoWriteNoUse_CannotInferType_Error) {
    const std::string src = "@main() := {\n"
                            "    x := _;\n"
                            "};\n";
    EXPECT_GT(runSemantic(src), 0);
    EXPECT_NE(msgs().find("cannot infer the type of untyped 'x := _'"), std::string::npos) << msgs();
}

// Глобальная/модульная нетипизированная `:= _` → Error (требуется явный тип).
TEST_F(UninitInferTest, GlobalUntypedUnderscore_Error) {
    const std::string src = "g := _;\n";
    EXPECT_GT(runSemantic(src), 0);
    EXPECT_NE(msgs().find("non-local 'g := _' requires an explicit type annotation"), std::string::npos) << msgs();
}

// Записи несовместимых категорий (число потом строка) → Error с требованием `:Any`.
TEST_F(UninitInferTest, IncompatibleCategories_Error) {
    const std::string src = "@main() := {\n"
                            "    x := _;\n"
                            "    x = 5;\n"
                            "    x = \"s\";\n"
                            "    @print('{}', x);\n"
                            "};\n";
    EXPECT_GT(runSemantic(src), 0);
    EXPECT_NE(msgs().find("cannot assign type 'StrWide'"), std::string::npos) << msgs();
    EXPECT_NE(msgs().find("Any := _"), std::string::npos) << msgs();
}

// Ветвление с записью не на всех путях (`if` без `else`) → чтение после Error.
TEST_F(UninitInferTest, BranchMissingWrite_Error) {
    const std::string src = "@main() := {\n"
                            "    x := _;\n"
                            "    if (1) {\n"
                            "        x = 5;\n"
                            "    };\n"
                            "    @print('{}', x);\n"
                            "};\n";
    EXPECT_GT(runSemantic(src), 0);
    EXPECT_NE(msgs().find("read before it is initialized"), std::string::npos) << msgs();
}

// Цикл while - недоказуемый путь → чтение после цикла Error.
TEST_F(UninitInferTest, LoopReadAfter_Error) {
    const std::string src = "@main() := {\n"
                            "    x := _;\n"
                            "    $i := 0;\n"
                            "    while ($i < 3) {\n"
                            "        x = 5;\n"
                            "        $i += 1;\n"
                            "    };\n"
                            "    @print('{}', x);\n"
                            "};\n";
    EXPECT_GT(runSemantic(src), 0);
    EXPECT_NE(msgs().find("read before it is initialized"), std::string::npos) << msgs();
}

} // namespace
