Tensor gemm(const Tensor& A, const Tensor& B, const Tensor& C, float alpha, float beta, int transA, int transB);
std::tuple<Tensor&, Tensor&, Tensor&> batchnorm_inplace(Tensor& X, const Tensor& scale, const Tensor& B, Tensor& input_mean, Tensor& input_var, const float epsilon, const float momentum); // {"schema": "batchnorm_inplace(Tensor(a!) X, Tensor scale, Tensor b, Tensor(b!) input_mean, Tensor(c!) input_var, float epsilon, float momentum) -> (Tensor(a!), Tensor(b!), Tensor(c!))", "dispatch": "False", "default": "True"}