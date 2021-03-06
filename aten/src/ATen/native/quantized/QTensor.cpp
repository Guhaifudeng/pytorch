#include <ATen/ATen.h>
#include <ATen/NativeFunctions.h>
#include <ATen/quantized/Quantizer.h>
#include <ATen/quantized/QTensorImpl.h>


namespace at {
namespace native {

Tensor quantize_linear_cpu(
    const Tensor& self, double scale, int64_t zero_point, ScalarType dtype) {
  auto quantizer = make_per_tensor_affine_quantizer(scale, zero_point, dtype);
  return quantizer->quantize(self);
}

Tensor quantize_linear_per_channel_cpu(
    const Tensor& self, const Tensor& scales, const Tensor& zero_points,
    IntArrayRef axis, ScalarType dtype) {
  TORCH_CHECK(scales.dim() == 1, "scale tensor must have dimension 1");
  TORCH_CHECK(zero_points.dim() == 1,
              "zero_points tensor must have dimension 1");
  TORCH_CHECK(scales.numel() == zero_points.numel(),
              "number of elements in scales and zero_points must match");
  TORCH_CHECK(axis.size() == 1, "only axis of size 1 is supported right now");
  float* scales_data = scales.data<float>();
  int32_t* zero_points_data = zero_points.data<int32_t>();
  std::vector<float> scale_vals(scales_data, scales_data + scales.numel());
  std::vector<int32_t> zero_point_vals(
      zero_points_data, zero_points_data + zero_points.numel());
  auto quantizer = make_per_channel_affine_quantizer(
      scale_vals, zero_point_vals, axis, dtype);
  return quantizer->quantize(self);
}

Tensor dequantize_quant(const Tensor& self) {
  return get_qtensorimpl(self)->quantizer()->dequantize(self);
}

Tensor dequantize_linear_cpu(
    const Tensor& self, double scale, int64_t zero_point, ScalarType dtype) {
  TORCH_CHECK(isQIntType(toQIntType(self.scalar_type())),
           "Scalar type for quantized Tensor must have same underlying type as input.");
  TORCH_CHECK(dtype == toQIntType(self.scalar_type()), "ScalarType argument must match the corresponding quantized scalar type of input integer Tensor");
  // scalar type of output Tensor is hard-coded as float
  Tensor f = at::empty(self.sizes(), self.options().dtype(at::kFloat));
  AT_DISPATCH_QINT_TYPES(
      toQIntType(self.scalar_type()), "dequantize_linear_cpu", [&]() {
        underlying_t* qdata = self.data<underlying_t>();
        auto* fdata = f.data<float>();
        for (int i = 0; i < self.numel(); ++i) {
          fdata[i] = (static_cast<float>(qdata[i]) - zero_point) * scale;
        }});
  return f;
}

Scalar q_scale_quant(const Tensor& self) {
  auto quantizer = get_qtensorimpl(self)->quantizer();
  TORCH_CHECK(quantizer->qscheme() == kPerTensorAffine);
  return Scalar(static_cast<PerTensorAffineQuantizer*>(quantizer.get())->scale());
}

Scalar q_zero_point_quant(const Tensor& self) {
  auto quantizer = get_qtensorimpl(self)->quantizer();
  TORCH_CHECK(quantizer->qscheme() == kPerTensorAffine);
  return Scalar(static_cast<PerTensorAffineQuantizer*>(quantizer.get())->zero_point());
}

Quantizer* quantizer(const Tensor& self) {
  return get_qtensorimpl(self)->quantizer().get();
}

Tensor int_repr_quant(const Tensor& self) {
  Tensor dst;
  // TODO: replace with TensorIterator
  auto self_c = self.contiguous();
  AT_DISPATCH_QINT_TYPES(
      self.scalar_type(), "int_repr", [&]() {
        dst = at::empty(self.sizes(), self.options().dtype(UNDERLYING_TYPE));
        underlying_t* self_data = reinterpret_cast<underlying_t *>(self_c.data<scalar_t>());
        underlying_t* dst_data = dst.data<underlying_t>();
        if (self.numel() > 0) {
          memcpy(dst_data, self_data, self.nbytes());
        }});
  return dst;
}

Tensor per_tensor_affine_qtensor_cpu(const Tensor& self, double scale, int64_t zero_point) {
  Tensor dst = at::_empty_affine_quantized(self.sizes(), self.options().dtype(toQIntType(self.scalar_type())), scale, zero_point);
  AT_DISPATCH_QINT_TYPES(dst.scalar_type(), "per_tensor_affine_qtensor", [&]() {
    underlying_t* self_data = self.data<underlying_t>();
    underlying_t* dst_data = reinterpret_cast<underlying_t *>(dst.data<scalar_t>());
    if (self.numel() > 0) {
      memcpy(dst_data, self_data, self.numel());
    }
  });
  return dst;
}

} // namespace native
} // namespace at
