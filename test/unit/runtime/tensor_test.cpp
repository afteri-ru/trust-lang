#include "types/tensors.hpp"
#include "runtime/tensor.hpp"
#include <gtest/gtest.h>
#include <stdexcept>

using namespace trust;

class TensorTypeTest : public ::testing::Test {
  protected:
    void SetUp() override { ASSERT_TRUE(ensure_tensor_runtime_loaded()) << "libtensor_cpu.so not found"; }
};

// Test Tensor type kind
TEST_F(TensorTypeTest, TensorKind) {
    Tensor t;
    EXPECT_EQ(t.kind(), TypeKind::DenseTensor);
}

TEST_F(TensorTypeTest, SparseTensorKind) {
    SparseTensor t;
    EXPECT_EQ(t.kind(), TypeKind::SparseTensor);
}

// Test Tensor to_string
TEST_F(TensorTypeTest, TensorToString) {
    Tensor t;
    EXPECT_EQ(t.to_string(false), "Tensor{}");
    EXPECT_EQ(t.to_string(true), "[Tensor] Tensor{}");
}

TEST_F(TensorTypeTest, SparseTensorToString) {
    SparseTensor t;
    EXPECT_EQ(t.to_string(false), "SparseTensor{}");
    EXPECT_EQ(t.to_string(true), "[SparseTensor] SparseTensor{}");
}

// Test TensorHandle
TEST_F(TensorTypeTest, TensorHandleEmpty) {
    TensorHandle h;
    EXPECT_FALSE(static_cast<bool>(h));
    EXPECT_FALSE(h.is<at::Tensor>());
    EXPECT_EQ(h.get<at::Tensor>(), nullptr);
}

// Test TorchTensor creation and operations
TEST_F(TensorTypeTest, TorchTensorCreation) {
    auto tensor = torch::ones({2, 3});
    TorchTensor tt(tensor);

    EXPECT_EQ(tt.native().sizes().vec(), (std::vector<int64_t>{2, 3}));
    EXPECT_TRUE(torch::all(tt.native() == 1).item<bool>());
}

TEST_F(TensorTypeTest, TorchTensorRoundTrip) {
    auto tensor = torch::randn({4, 5});
    TorchTensor tt1(tensor);

    TensorHandle handle = tt1.as_var_handle();
    EXPECT_TRUE(static_cast<bool>(handle));

    TorchTensor tt2(handle);
    EXPECT_EQ(tt2.native().sizes().vec(), (std::vector<int64_t>{4, 5}));
}

TEST_F(TensorTypeTest, TorchTensorNativeAccess) {
    auto tensor = torch::arange(0, 6).reshape({2, 3});
    TorchTensor tt(tensor);

    auto sum = tt.native().sum().item<double>();
    EXPECT_DOUBLE_EQ(sum, 0.0 + 1.0 + 2.0 + 3.0 + 4.0 + 5.0);
}

TEST_F(TensorTypeTest, TorchTensorMatrixMultiply) {
    auto a = torch::ones({2, 3});
    auto b = torch::ones({3, 4});

    TorchTensor ta(a);
    TorchTensor tb(b * 2);

    auto result = torch::matmul(ta.native(), tb.native());
    EXPECT_EQ(result.sizes().vec(), (std::vector<int64_t>{2, 4}));
    EXPECT_TRUE(torch::all(result == 6).item<bool>());
}

// Test TensorHandle with shared_ptr
TEST_F(TensorTypeTest, TensorHandleWithSharedPtr) {
    auto tensor = std::make_shared<at::Tensor>(torch::zeros({3, 3}));
    TensorHandle handle(tensor);

    EXPECT_TRUE(static_cast<bool>(handle));
    EXPECT_TRUE(handle.is<at::Tensor>());
    EXPECT_NE(handle.get<at::Tensor>(), nullptr);

    // Modify through handle
    auto* ptr = handle.get<at::Tensor>();
    *ptr = torch::ones({3, 3});

    TorchTensor tt(handle);
    EXPECT_TRUE(torch::all(tt.native() == 1).item<bool>());
}

// Test TensorHandle swap
TEST_F(TensorTypeTest, TensorHandleSwap) {
    auto t1 = std::make_shared<at::Tensor>(torch::zeros({2, 2}));
    auto t2 = std::make_shared<at::Tensor>(torch::ones({3, 3}));

    TensorHandle h1(t1);
    TensorHandle h2(t2);

    swap(h1, h2);

    EXPECT_EQ(h1.get<at::Tensor>()->sizes().vec(), (std::vector<int64_t>{3, 3}));
    EXPECT_EQ(h2.get<at::Tensor>()->sizes().vec(), (std::vector<int64_t>{2, 2}));
}

// Test Tensor set on handle
TEST_F(TensorTypeTest, TensorHandleSet) {
    auto t1 = std::make_shared<at::Tensor>(torch::zeros({2, 2}));
    auto t2 = std::make_shared<at::Tensor>(torch::ones({4, 4}));

    TensorHandle h(t1);
    EXPECT_EQ(h.get<at::Tensor>()->sizes().vec(), (std::vector<int64_t>{2, 2}));

    h.set<at::Tensor>(t2);
    EXPECT_EQ(h.get<at::Tensor>()->sizes().vec(), (std::vector<int64_t>{4, 4}));
}

// Test PluginAutoLoaded
TEST_F(TensorTypeTest, PluginAutoLoaded) {
    ASSERT_NO_THROW(ensure_tensor_runtime_loaded());
}