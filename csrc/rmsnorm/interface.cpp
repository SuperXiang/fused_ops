#include <torch/extension.h>
#include <vector>
#include <cassert>

namespace {
void compute_n1_n2(
    at::Tensor input,
    at::IntArrayRef normalized_shape,
    int& n1,
    int& n2)
{
    int idiff = input.ndimension() - normalized_shape.size();
    n2 = 1;
    for (int i = 0;  i < (int)normalized_shape.size();  ++i) {
	    assert( input.sizes()[i+idiff] == normalized_shape[i] );
	    n2 *= normalized_shape[i];
    }
    n1 = 1;
    for (int i = 0;  i < idiff;  ++i) {
	    n1 *= input.sizes()[i];
    }
}

void check_args(
    at::IntArrayRef normalized_shape,
    at::Tensor gamma
    )
{
    TORCH_CHECK(!gamma.defined() || gamma.sizes().equals(normalized_shape));
}

void check_args(
    at::Tensor input,
    at::IntArrayRef normalized_shape,
    int& n1,
    int& n2
    )
{
    int64_t normalized_ndim = normalized_shape.size();

    if (normalized_ndim < 1) {
      std::stringstream ss;
      ss << "Expected normalized_shape to be at least 1-dimensional, i.e., "
         << "containing at least one element, but got normalized_shape="
         << normalized_shape;
      throw std::runtime_error(ss.str());
    }

    auto input_shape = input.sizes();
    auto input_ndim = input.dim();

    if (input_ndim < normalized_ndim ||
        !input_shape.slice(input_ndim - normalized_ndim).equals(normalized_shape)) {
      std::stringstream ss;
      ss << "Given normalized_shape=" << normalized_shape
         << ", expected input with shape [*";
      for (auto size : normalized_shape) {
        ss << ", " << size;
      }
      ss << "], but got input of size" << input_shape;
      throw std::runtime_error(ss.str());
    }

    compute_n1_n2(input,normalized_shape,n1,n2);
}


void check_args(
    at::Tensor input,
    at::IntArrayRef normalized_shape,
    at::Tensor gamma,
    int& n1,
    int& n2
    )
{
    check_args(input,normalized_shape,n1,n2);
    check_args(normalized_shape,gamma);
}
}

void cuda_rms_norm(
    at::Tensor* output,
    at::Tensor* invvar,
    at::Tensor* input,
    int n1,
    int n2,
    at::IntArrayRef normalized_shape,
    at::Tensor* gamma,
    double epsilon);

#define CHECK_CUDA(x) TORCH_CHECK(x.is_cuda(), #x " must be a CUDA tensor")
#define CHECK_CONTIGUOUS(x) TORCH_CHECK(x.is_contiguous(), #x " must be contiguous")
#define CHECK_INPUT(x) CHECK_CUDA(x); CHECK_CONTIGUOUS(x)

std::vector<at::Tensor> rms_norm(
    at::Tensor input,
    at::IntArrayRef normalized_shape,
    at::Tensor gamma,
    double epsilon) {
  CHECK_INPUT(input);
  CHECK_INPUT(gamma);
  int n1,n2;
  check_args(input,normalized_shape,gamma,n1,n2);
  TORCH_CHECK(n2 == 64 || n2 == 128 || n2 == 256 || n2 == 384 || n2 == 512 || n2 == 768 || n2 == 1024 || n2 == 1280 ||
              n2 == 1536 || n2 == 1792 || n2 == 2048, "dimension is not supported");
  at::Tensor output = at::empty_like(input);
  at::Tensor invvar = at::empty({n1}, input.options().dtype((input.scalar_type()==at::ScalarType::Half || input.scalar_type()==at::ScalarType::BFloat16) ? at::ScalarType::Float : input.scalar_type()));
  cuda_rms_norm(&output,&invvar,&input,n1,n2,
      normalized_shape,&gamma,epsilon);
  return {output, invvar};
}

void cuda_rms_norm_gradient(
    at::Tensor* dout,
    at::Tensor* invvar,
    at::Tensor* input,
    int n1,
    int n2,
    at::IntArrayRef normalized_shape,
    at::Tensor* gamma,
    double epsilon,
    at::Tensor* grad_input
    );

at::Tensor rms_norm_gradient(
    at::Tensor dout,
    at::Tensor invvar,
    at::Tensor input,
    at::IntArrayRef normalized_shape,
    at::Tensor gamma,
    double epsilon) {
  CHECK_INPUT(dout);
  CHECK_INPUT(invvar);
  CHECK_INPUT(input);
  CHECK_INPUT(gamma);
  int n1,n2;
  check_args(input,normalized_shape,gamma,n1,n2);
  TORCH_CHECK(n2 == 64 || n2 == 128 || n2 == 256 || n2 == 384 || n2 == 512 || n2 == 768 || n2 == 1024 || n2 == 1280 ||
              n2 == 1536 || n2 == 1792 || n2 == 2048, "dimension is not supported");
  at::Tensor grad_input = at::empty_like(input);
  cuda_rms_norm_gradient(&dout,&invvar,&input,n1,n2,
      normalized_shape,&gamma,epsilon,
      &grad_input);
  return grad_input;
}

PYBIND11_MODULE(TORCH_EXTENSION_NAME, m) {
  m.def("forward", &rms_norm, "RMSNorm fast forward (CUDA)");
  m.def("backward", &rms_norm_gradient, "RMSNorm fast backward (CUDA)");
}
