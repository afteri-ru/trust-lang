#include "runtime/tensor.hpp"
#include "runtime/var.hpp"
#include <gtest/gtest.h>
#include <stdexcept>

using namespace trust;

class TensorTest : public ::testing::Test {
  protected:
    void SetUp() override { ensure_tensor_runtime_loaded(); }
};

TEST_F(TensorTest, CreateFromVar) {
    auto tensor = torch::ones({2, 3});
    Var o(Tensor{std::make_shared<at::Tensor>(tensor)});

    TorchTensor t = TorchTensor::from_var(o);
    EXPECT_EQ(t.native().sizes().vec(), (std::vector<int64_t>{2, 3}));
    EXPECT_TRUE(torch::all(t.native() == 1).item<bool>());
}

TEST_F(TensorTest, RoundTripVar) {
    auto tensor = torch::randn({4, 5});
    Var o(Tensor{std::make_shared<at::Tensor>(tensor)});

    TorchTensor t = TorchTensor::from_var(o);
    Var out(t.as_var_handle());

    EXPECT_TRUE(out.is<Tensor>());
    TorchTensor t2 = TorchTensor::from_var(out);
    EXPECT_EQ(t2.native().sizes().vec(), (std::vector<int64_t>{4, 5}));
}

TEST_F(TensorTest, NativeAccess) {
    auto tensor = torch::arange(0, 6).reshape({2, 3});
    auto handle = Tensor{std::make_shared<at::Tensor>(tensor)};
    Var o(handle);

    TorchTensor t = TorchTensor::from_var(o);
    auto sum = t.native().sum().item<double>();
    EXPECT_DOUBLE_EQ(sum, 0.0 + 1.0 + 2.0 + 3.0 + 4.0 + 5.0);
}

TEST_F(TensorTest, TensorMultiply) {
    auto a = torch::ones({2, 3});
    auto b = torch::ones({3, 4});
    Var oa(Tensor{std::make_shared<at::Tensor>(a)});
    Var ob(Tensor{std::make_shared<at::Tensor>(b * 2)});

    TorchTensor ta = TorchTensor::from_var(oa);
    TorchTensor tb = TorchTensor::from_var(ob);

    auto result = torch::matmul(ta.native(), tb.native());
    EXPECT_EQ(result.sizes().vec(), (std::vector<int64_t>{2, 4}));
    EXPECT_TRUE(torch::all(result == 6).item<bool>());
}

TEST_F(TensorTest, PluginAutoLoaded) {
    ASSERT_NO_THROW(ensure_tensor_runtime_loaded());
}

TEST_F(TensorTest, FromNonTensorVarThrows) {
    Var o(42);
    EXPECT_THROW(TorchTensor::from_var(o), std::runtime_error);
}