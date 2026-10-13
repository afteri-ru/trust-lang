#include "runtime/torch/tensor.hpp"
#include "runtime/trust/tensor.hpp"

#include <gtest/gtest.h>

#include <vector>

using namespace trust;

TEST(TensorValueTest, CreateDtypeShape) {
    Tensor<float> t({2, 3}, {1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F});
    EXPECT_EQ(t.dtype(), TRUST_TENSOR_DTYPE_FLOAT32);
    EXPECT_EQ(t.rank(), 2);
    EXPECT_EQ(t.numel(), 6);
    EXPECT_EQ(t.shape(), (std::vector<int64_t>{2, 3}));
}

TEST(TensorValueTest, ZeroInitialized) {
    Tensor<int32_t> t({2, 2});
    EXPECT_EQ(t.numel(), 4);
    EXPECT_EQ(t(0, 0), 0);
    EXPECT_EQ(t(1, 1), 0);
}

TEST(TensorValueTest, ElementAccessAndWrite) {
    Tensor<float> t({2, 2}, {1.0F, 2.0F, 3.0F, 4.0F});
    EXPECT_FLOAT_EQ(t[0], 1.0F);
    EXPECT_FLOAT_EQ(t(1, 0), 3.0F);
    t[1] = 20.0F;
    t(1, 1) = 40.0F;
    EXPECT_FLOAT_EQ(t(0, 1), 20.0F);
    EXPECT_FLOAT_EQ(t(1, 1), 40.0F);
}

TEST(TensorValueTest, NegativeIndex) {
    Tensor<int64_t> t({2, 3}, {1, 2, 3, 4, 5, 6});
    EXPECT_EQ(t(0, -1), 3);
    EXPECT_EQ(t(-1, 0), 4);
    EXPECT_THROW(
        {
            const auto value = static_cast<int64_t>(t(0, 3));
            (void)value;
        },
        std::runtime_error);
}

TEST(TensorValueTest, Arithmetic) {
    Tensor<float> a({2, 2}, {1.0F, 2.0F, 3.0F, 4.0F});
    Tensor<float> b({2, 2}, {10.0F, 20.0F, 30.0F, 40.0F});

    const Tensor<float> sum = a + b;
    EXPECT_FLOAT_EQ(sum(0, 0), 11.0F);
    EXPECT_FLOAT_EQ(sum(1, 1), 44.0F);

    const Tensor<float> scaled = a * 2.0F;
    EXPECT_FLOAT_EQ(scaled(1, 0), 6.0F);

    const Tensor<float> reversed = 10.0F - a;
    EXPECT_FLOAT_EQ(reversed(0, 0), 9.0F);
}

TEST(TensorValueTest, MatmulSumReshape) {
    Tensor<float> a({2, 3}, {1, 2, 3, 4, 5, 6});
    Tensor<float> b({3, 2}, {1, 2, 3, 4, 5, 6});

    const Tensor<float> m = a.matmul(b);
    EXPECT_EQ(m.shape(), (std::vector<int64_t>{2, 2}));
    EXPECT_FLOAT_EQ(m(0, 0), 22.0F);
    EXPECT_FLOAT_EQ(m(1, 1), 64.0F);

    const Tensor<float> s = a.sum();
    EXPECT_EQ(s.numel(), 1);
    EXPECT_FLOAT_EQ(s.item(), 21.0F);

    const Tensor<float> r = a.reshape({3, 2});
    EXPECT_EQ(r.shape(), (std::vector<int64_t>{3, 2}));
    EXPECT_FLOAT_EQ(r(2, 1), 6.0F);
}

TEST(TorchTensorTest, RoundTrip) {
    auto native = torch::ones({2, 3});
    TorchTensor tt(native);
    EXPECT_EQ(tt.native().sizes().vec(), (std::vector<int64_t>{2, 3}));

    TensorHandle handle = tt.as_var_handle();
    EXPECT_TRUE(static_cast<bool>(handle));

    TorchTensor back(handle);
    EXPECT_TRUE(torch::all(back.native() == 1).item<bool>());
}
