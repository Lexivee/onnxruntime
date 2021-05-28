# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT License.

# Import external libraries.
import copy
import onnxruntime
import pytest
import torch
from torch.nn.parameter import Parameter

# Import ORT modules.
import _test_helpers
from onnxruntime.training.ortmodule import ORTModule
from onnxruntime.training.ortmodule._graph_execution_manager_factory import GraphExecutionManagerFactory

torch.manual_seed(1)
onnxruntime.set_seed(1)


def set_onnx_fallthrough_export_type(module):
    onnx_export_type = torch.onnx.OperatorExportTypes.ONNX_FALLTHROUGH
    module._execution_manager = GraphExecutionManagerFactory(
        module._module_metadata.flattened_module, onnx_export_type=onnx_export_type)


def run_with_pytorch_on_device(device, model, input_list, label_input, is_eval_mode=False):
    model.to(device)
    if is_eval_mode:
        model.eval()
    else:
        model.train()

    inputs_on_device = [input_.to(device) for input_ in input_list]
    output = model(*inputs_on_device)
    forward_outputs = [output]
    grad_outputs = []

    if not is_eval_mode:
        criterion = torch.nn.MSELoss()
        target = label_input.to(device)
        loss = criterion(output, target)
        loss.backward()
        for name, param in model.named_parameters():
            if param.requires_grad:
                grad_outputs.append(param.grad)
    return forward_outputs, grad_outputs


def run_with_ort_on_device(device, model, input_list, label_input, is_eval_mode=False):
    model = copy.deepcopy(model)
    model.to(device)
    model = ORTModule(model)
    set_onnx_fallthrough_export_type(model)
    if is_eval_mode:
        model.eval()
    else:
        model.train()

    inputs_on_device = [input_.to(device) for input_ in input_list]
    output = model(*inputs_on_device)
    forward_outputs = [output]
    grad_outputs = []

    if not is_eval_mode:
        criterion = torch.nn.MSELoss()
        target = label_input.to(device)
        loss = criterion(output, target)
        loss.backward()
        for name, param in model.named_parameters():
            if param.requires_grad:
                grad_outputs.append(param.grad)
    return forward_outputs, grad_outputs


def compare_tensor_list(val_list_a, val_list_b):
    for val_a, val_b in zip(val_list_a, val_list_b):
        _test_helpers.assert_values_are_close(
            val_a, val_b, atol=1e-7, rtol=1e-6)


def run_training_test_and_compare(pt_model_builder_func, pt_model_inputs_generator, pt_model_label_input, ignore_grad_compare=False):
    cpu = torch.device("cpu")

    def cpu_barrier_func():
        pass
    run_training_test_on_device_and_compare(
        cpu, pt_model_builder_func, pt_model_inputs_generator, pt_model_label_input, cpu_barrier_func, ignore_grad_compare)

    def cuda_barrier_func():
        torch.cuda.synchronize()
    cuda = torch.device('cuda:0')
    run_training_test_on_device_and_compare(
        cuda, pt_model_builder_func, pt_model_inputs_generator, pt_model_label_input, cuda_barrier_func, ignore_grad_compare)


def run_training_test_on_device_and_compare(device, pt_model_builder_func, pt_model_inputs_generator, pt_model_label_input, barrier_func, ignore_grad_compare=False):
    repeats = 16
    for i in range(repeats):
        m = pt_model_builder_func()
        x = pt_model_inputs_generator()

        m_ort = copy.deepcopy(m)
        x_ort = copy.deepcopy(x)

        outputs, grads = run_with_pytorch_on_device(
            device, m, [x], pt_model_label_input)
        barrier_func()

        outputs_ort, grads_ort = run_with_ort_on_device(
            device, m_ort, [x_ort], pt_model_label_input)
        barrier_func()

        val_list_a = [o.detach().cpu() for o in outputs if o is not None]
        val_list_b = [o.detach().cpu() for o in outputs_ort if o is not None]
        compare_tensor_list(val_list_a, val_list_b)

        # For some test, it is expected the diff might be big due to inconsistent computation orders.
        if ignore_grad_compare is False:
            val_list_a = [o.detach().cpu() for o in grads if o is not None]
            val_list_b = [o.detach().cpu() for o in grads_ort if o is not None]
            compare_tensor_list(val_list_a, val_list_b)


def run_evaluate_test_and_compare(pt_model_builder_func, pt_model_inputs_generator, pt_model_label_input):
    cpu = torch.device("cpu")

    def cpu_barrier_func():
        pass

    run_evaluate_test_on_device_and_compare(
        cpu, pt_model_builder_func, pt_model_inputs_generator, pt_model_label_input, cpu_barrier_func)

    def cuda_barrier_func():
        torch.cuda.synchronize()
        pass

    cuda = torch.device('cuda:0')
    run_evaluate_test_on_device_and_compare(
        cuda, pt_model_builder_func, pt_model_inputs_generator, pt_model_label_input, cuda_barrier_func)


def run_evaluate_test_on_device_and_compare(device, pt_model_builder_func, pt_model_inputs_generator, pt_model_label_input, barrier_func):
    repeats = 16
    for i in range(repeats):
        m = pt_model_builder_func()
        x = pt_model_inputs_generator()

        m_ort = copy.deepcopy(m)
        x_ort = copy.deepcopy(x)

        outputs, grads = run_with_pytorch_on_device(
            device, m, [x], pt_model_label_input, is_eval_mode=True)
        barrier_func()

        outputs_ort, grads_ort = run_with_ort_on_device(
            device, m_ort, [x_ort], pt_model_label_input, is_eval_mode=True)
        barrier_func()

        val_list_a = [o.detach().cpu() for o in outputs if o is not None]
        val_list_b = [o.detach().cpu() for o in outputs_ort if o is not None]
        compare_tensor_list(val_list_a, val_list_b)


def test_GeLU():
    @torch.jit.script
    def bias_gelu(bias, y):
        x = bias + y
        return x * 0.5 * (1.0 + torch.tanh(0.79788456 * x * (1 + 0.044715 * x * x)))

    @torch.jit.script
    def bias_gelu_backward(g, bias, y):
        x = bias + y
        tanh_out = torch.tanh(0.79788456 * x * (1 + 0.044715 * x * x))
        ff = 0.5 * x * ((1 - tanh_out * tanh_out) * (0.79788456 +
                        0.1070322243 * x * x)) + 0.5 * (1 + tanh_out)
        return ff*g

    class GeLUFunction(torch.autograd.Function):
        @staticmethod
        def forward(ctx, input, bias):
            ctx.save_for_backward(input, bias)
            return bias_gelu(bias, input)

        @staticmethod
        def backward(ctx, grad_output):
            input, bias = ctx.saved_tensors
            tmp = bias_gelu_backward(grad_output, bias, input)
            return tmp, tmp

    class GeLUModel(torch.nn.Module):
        def __init__(self, output_size):
            super(GeLUModel, self).__init__()
            self.relu = GeLUFunction.apply
            self.bias = Parameter(torch.empty(
                output_size,
                device=torch.cuda.current_device(),
                dtype=torch.float))

            # Always initialize bias to zero.
            with torch.no_grad():
                self.bias.uniform_()

        def forward(self, model_input):
            out = self.relu(model_input, self.bias)
            return out

    output_size = 1024

    def model_builder():
        return GeLUModel(output_size)

    def input_generator():
        return torch.randn(output_size, dtype=torch.float)

    # generate a label that have same shape as forward output.
    label_input = torch.ones([output_size])

    run_training_test_and_compare(model_builder, input_generator, label_input)


def test_MegatronF():
    # MegatronGFunction is tested in distributed test files.
    class MegatronFFunction(torch.autograd.Function):
        @staticmethod
        def forward(ctx, input_):
            return input_

        @staticmethod
        def backward(ctx, grad_output):
            # Bypass the reduce as if we are using only 1 GPU.
            return grad_output

    class MegatronFModel(torch.nn.Module):
        def __init__(self, output_size):
            super(MegatronFModel, self).__init__()
            self.copy_ = MegatronFFunction.apply
            self.bias = Parameter(torch.empty(
                output_size,
                device=torch.cuda.current_device(),
                dtype=torch.float))

            # Always initialize bias to zero.
            with torch.no_grad():
                self.bias.uniform_()

        def forward(self, model_input):
            model_input = model_input + self.bias
            out = self.copy_(model_input)
            return out

    output_size = 1024

    def model_builder():
        return MegatronFModel(output_size)

    def input_generator():
        return torch.randn(output_size, dtype=torch.float)

    # generate a label that have same shape as forward output.
    label_input = torch.ones([output_size])

    run_training_test_and_compare(model_builder, input_generator, label_input)


def test_ScalarAndTuple():
    class ScalarAndTupleFunction(torch.autograd.Function):
        @staticmethod
        def forward(ctx, input, alpha, beta, gamma):
            ctx.save_for_backward(input)
            ctx.alpha = alpha
            ctx.beta = beta
            ctx.gamma = gamma
            return alpha * beta[0] * beta[1] * gamma * input.clamp(min=0)

        @staticmethod
        def backward(ctx, grad_output):
            input, = ctx.saved_tensors
            alpha = ctx.alpha
            beta = ctx.beta
            gamma = ctx.gamma
            grad_input = grad_output.clone()
            grad_input[input < 0] = 0
            return alpha * beta[0] * beta[1] * gamma * grad_input, None, None, None

    class ScalarAndTupleModel(torch.nn.Module):
        def __init__(self, output_size):
            super(ScalarAndTupleModel, self).__init__()
            self.activation = ScalarAndTupleFunction.apply
            self.linear_a = torch.nn.Linear(output_size, output_size)
            self.linear_b = torch.nn.Linear(output_size, output_size)

        def forward(self, x):
            h = self.linear_a(x)
            h = self.activation(h, 5.0, (-1.0, 2.0), -1.0)
            h = self.linear_b(h)
            return h

    output_size = 2

    def model_builder():
        return ScalarAndTupleModel(output_size)

    def input_generator():
        return torch.randn(output_size, dtype=torch.float)

    # generate a label that have same shape as forward output.
    label_input = torch.ones([output_size])

    run_training_test_and_compare(model_builder, input_generator, label_input)


@pytest.mark.skip(reason="This test is not correct. All tensors modified by in-place operattions should be mark_dirty(...).")
def test_InplaceUpdateInputAsOutputNotRequireGrad():
    class InplaceUpdateInputAsOutputNotRequireGradFunction(torch.autograd.Function):
        @staticmethod
        def forward(ctx, bias, inplace_update_input):
            # without mark_ditry, the inner computation graph is extracted into
            # another subgraph, which is a duplicated computation with the PythonOp.
            # so for the weights that are used twice BUT SHOULD only used once,
            # the gradients are almost 2x than PyTorch's grad, this is the reason we
            # ignore the gradient compare here.
            ctx.save_for_backward(inplace_update_input, bias)
            return inplace_update_input.add_(3 * bias)

        @staticmethod
        def backward(ctx, grad_output):
            return grad_output, None

    class InplaceUpdateInputAsOutputNotRequireGradModel(torch.nn.Module):
        def __init__(self, output_size):
            super(InplaceUpdateInputAsOutputNotRequireGradModel, self).__init__()
            self.inplace_op = InplaceUpdateInputAsOutputNotRequireGradFunction.apply
            self.bias = Parameter(torch.empty(
                output_size,
                device=torch.cuda.current_device(),
                dtype=torch.float))

            # Always initialize bias to zero.
            with torch.no_grad():
                self.bias.uniform_()

        def forward(self, model_input):
            x = model_input.mul(2)
            y1 = self.inplace_op(self.bias, x)  # x did not require grad
            y2 = x.add(self.bias)
            out = y1 + y2
            return out

    output_size = 1024

    def model_builder():
        return InplaceUpdateInputAsOutputNotRequireGradModel(output_size)

    def input_generator():
        return torch.randn(output_size, dtype=torch.float)

    # generate a label that have same shape as forward output.
    label_input = torch.ones([output_size])

    # Test when input is in-place updated, but does not require gradient.
    run_training_test_and_compare(
        model_builder, input_generator, label_input, ignore_grad_compare=True)


@pytest.mark.skip(reason="This test is not correct. All tensors modified by in-place operattions should be mark_dirty(...).")
def test_InplaceUpdateInputNotAsOutputNotRequireGrad():
    class InplaceUpdateInputNotAsOutputNotRequireGradFunction(torch.autograd.Function):
        @staticmethod
        def forward(ctx, bias, inplace_update_input):
            ctx.save_for_backward(inplace_update_input, bias)
            inplace_update_input.add_(3 * bias)
            return inplace_update_input * 5

        @staticmethod
        def backward(ctx, grad_output):
            return grad_output, None

    class InplaceUpdateInputNotAsOutputNotRequireGradModel(torch.nn.Module):
        def __init__(self, output_size):
            super(InplaceUpdateInputNotAsOutputNotRequireGradModel, self).__init__()
            self.inplace_op = InplaceUpdateInputNotAsOutputNotRequireGradFunction.apply
            self.bias = Parameter(torch.empty(
                output_size,
                device=torch.cuda.current_device(),
                dtype=torch.float))

            # Always initialize bias to zero.
            with torch.no_grad():
                self.bias.uniform_()

        def forward(self, model_input):
            x = model_input.mul(2)
            y1 = self.inplace_op(self.bias, x)
            y2 = x.add(self.bias)
            out = y1 + y2
            return out

    output_size = 1024

    def model_builder():
        return InplaceUpdateInputNotAsOutputNotRequireGradModel(output_size)

    def input_generator():
        return torch.randn(output_size, dtype=torch.float)

    # generate a label that have same shape as forward output.
    label_input = torch.ones([output_size])

    # Without mark_ditry, the inner computation graph is extracted into another subgraph, which is a duplicated computation with the PythonOp.
    # So for the weights that are used twice BUT SHOULD only used once, the gradients are almost 2x than PyTorch's grad, this is the reason we
    # ignore the gradient compare here.
    run_training_test_and_compare(
        model_builder, input_generator, label_input, ignore_grad_compare=True)


def test_InplaceUpdateInputAsOutputNotRequireGradWithMarkDirty():
    class InplaceUpdateInputAsOutputNotRequireGradWithMarkDirtyFunction(torch.autograd.Function):
        @staticmethod
        def forward(ctx, bias, inplace_update_input):
            ctx.save_for_backward(inplace_update_input, bias)
            ctx.mark_dirty(inplace_update_input)
            # Be noted: if we make the input dirty, we must also put the input in outputs, otherwise, we will get such an error:
            # "RuntimeError: Some elements marked as dirty during the forward method were not returned as output.
            # The inputs that are modified inplace must all be outputs of the Function.""
            return inplace_update_input.add_(3 * bias)

        @staticmethod
        def backward(ctx, grad_output):
            # Bypass the reduce if we are using only 1 GPU.
            return grad_output, None

    class InplaceUpdateInputAsOutputNotRequireGradWithMarkDirtyModel(torch.nn.Module):
        def __init__(self, output_size):
            super(InplaceUpdateInputAsOutputNotRequireGradWithMarkDirtyModel,
                  self).__init__()
            self.inplace_op = InplaceUpdateInputAsOutputNotRequireGradWithMarkDirtyFunction.apply
            self.bias = Parameter(torch.empty(
                output_size,
                device=torch.cuda.current_device(),
                dtype=torch.float))

            # Always initialize bias to zero.
            with torch.no_grad():
                self.bias.uniform_()

        def forward(self, model_input):
            x = model_input.mul(2)
            y1 = self.inplace_op(self.bias, x)
            y2 = x.add(self.bias)
            out = y1 + y2
            return out

    output_size = 1024

    def model_builder():
        return InplaceUpdateInputAsOutputNotRequireGradWithMarkDirtyModel(output_size)

    def input_generator():
        return torch.randn(output_size, dtype=torch.float)

    # generate a label that have same shape as forward output.
    label_input = torch.ones([output_size])

    run_training_test_and_compare(model_builder, input_generator, label_input)


def test_InplaceUpdateInputAsOutputRequireGrad():
    class InplaceUpdateInputAsOutputRequireGradFunction(torch.autograd.Function):
        @staticmethod
        def forward(ctx, bias, inplace_update_input):
            ctx.save_for_backward(inplace_update_input, bias)
            # Be noted: if we make the input dirty, we must also put the input in outputs, otherwise, we will get such an error:
            # "RuntimeError: Some elements marked as dirty during the forward method were not returned as output. The inputs that are modified inplace must all be outputs of the Function.""
            return inplace_update_input.add_(3 * bias)

        @staticmethod
        def backward(ctx, grad_output):
            return grad_output, grad_output

    class InplaceUpdateInputAsOutputRequireGradModel(torch.nn.Module):
        def __init__(self, output_size):
            super(InplaceUpdateInputAsOutputRequireGradModel, self).__init__()
            self.inplace_op = InplaceUpdateInputAsOutputRequireGradFunction.apply
            self.bias = Parameter(torch.empty(
                output_size,
                device=torch.cuda.current_device(),
                dtype=torch.float))

            # Always initialize bias to zero.
            with torch.no_grad():
                self.bias.uniform_()

        def forward(self, model_input):
            x = model_input + self.bias
            y1 = self.inplace_op(self.bias, x)
            y2 = x.add(self.bias)
            out = y1 + y2
            return out

    output_size = 1024

    def model_builder():
        return InplaceUpdateInputAsOutputRequireGradModel(output_size)

    def input_generator():
        return torch.randn(output_size, dtype=torch.float)

    # generate a label that have same shape as forward output.
    label_input = torch.ones([output_size])

    # Test when input is in-place updated, but does require gradient.
    #
    # without mark_ditry, the inner computation graph is extracted into another subgraph, which is a
    # duplicated computation with the PythonOp.  Thus, for the weights that are used twice BUT SHOULD
    # only used once, the gradients are almost 2x than PyTorch's grad, this is the reason we
    # ignore the gradient compare here.
    run_training_test_and_compare(
        model_builder, input_generator, label_input, ignore_grad_compare=True)


def test_InplaceUpdateInputNotAsOutputRequireGrad():
    class InplaceUpdateInputNotAsOutputRequireGradFunction(torch.autograd.Function):
        # without mark_ditry, the inner computation graph is extracted into another subgraph, which is a duplicated computation with the PythonOp.
        # so for the weights that are used twice BUT SHOULD only used once, the gradients are almost 2x than PyTorch's grad, this is the reason we
        # ignore the gradient compare here.
        @staticmethod
        def forward(ctx, bias, inplace_update_input):
            ctx.save_for_backward(inplace_update_input, bias)
            inplace_update_input.add_(3 * bias)
            return inplace_update_input * 5

        @staticmethod
        def backward(ctx, grad_output):
            return grad_output, grad_output

    class InplaceUpdateInputNotAsOutputRequireGradModel(torch.nn.Module):
        def __init__(self, output_size):
            super(InplaceUpdateInputNotAsOutputRequireGradModel, self).__init__()
            self.inplace_op = InplaceUpdateInputNotAsOutputRequireGradFunction.apply
            self.bias = Parameter(torch.empty(
                output_size,
                device=torch.cuda.current_device(),
                dtype=torch.float))

            # Always initialize bias to zero.
            with torch.no_grad():
                self.bias.uniform_()

        def forward(self, model_input):
            x = model_input + self.bias
            y1 = self.inplace_op(self.bias, x)
            y2 = x.add(self.bias)
            out = y1 + y2
            return out

    output_size = 1024

    def model_builder():
        return InplaceUpdateInputNotAsOutputRequireGradModel(output_size)

    def input_generator():
        return torch.randn(output_size, dtype=torch.float)

    # generate a label that have same shape as forward output.
    label_input = torch.ones([output_size])

    # This case is known to have an warning message: "The output torch tensor @140214094625024, 140212816617984
    # should reuse the input torch tensor @140214095996104, 140212816617984 but actually not." It seems
    # if we don't have mark_dirty() in auto grad forward, the result is not using the input_,
    # (maybe a view of it, because data address is same)
    run_training_test_and_compare(
        model_builder, input_generator, label_input, ignore_grad_compare=True)

##########################################################################################


def test_InplaceUpdateInputAsOutputRequireGradWithMarkDirty():
    class InplaceUpdateInputAsOutputRequireGradWithMarkDirtyFunction(torch.autograd.Function):
        @staticmethod
        def forward(ctx, bias, inplace_update_input):
            ctx.save_for_backward(inplace_update_input, bias)
            ctx.mark_dirty(inplace_update_input)
            # Be noted: if we make the input dirty, we must also put the input in outputs,
            # otherwise, we will get such an error:
            # "RuntimeError: Some elements marked as dirty during the forward method were not returned as output.
            # The inputs that are modified inplace must all be outputs of the Function.""
            return inplace_update_input.add_(3 * bias)

        @staticmethod
        def backward(ctx, grad_output):
            return grad_output, grad_output

    class InplaceUpdateInputAsOutputRequireGradWithMarkDirtyModel(torch.nn.Module):
        def __init__(self, output_size):
            super(InplaceUpdateInputAsOutputRequireGradWithMarkDirtyModel,
                  self).__init__()
            self.inplace_op = InplaceUpdateInputAsOutputRequireGradWithMarkDirtyFunction.apply
            self.bias = Parameter(torch.empty(
                output_size,
                device=torch.cuda.current_device(),
                dtype=torch.float))

            # Always initialize bias to zero.
            with torch.no_grad():
                self.bias.uniform_()

        def forward(self, model_input):
            x = model_input + self.bias
            y1 = self.inplace_op(self.bias, x)
            y2 = x.add(self.bias)
            out = y1 + y2
            return out

    output_size = 1024

    def model_builder():
        return InplaceUpdateInputAsOutputRequireGradWithMarkDirtyModel(output_size)

    def input_generator():
        return torch.randn(output_size, dtype=torch.float)

    # generate a label that have same shape as forward output.
    label_input = torch.ones([output_size])

    run_training_test_and_compare(model_builder, input_generator, label_input)


def test_EvalTest():
    class EvalTestFunction(torch.autograd.Function):
        @staticmethod
        # bias is an optional argument
        def forward(ctx, x):
            ctx.save_for_backward(x)
            return x * 0.5 * (1.0 + torch.tanh(0.79788456 * x * (1 + 0.044715 * x * x)))

        @staticmethod
        def backward(ctx, grad_output):
            x = ctx.saved_tensors
            return None

    class EvalTestModel(torch.nn.Module):
        def __init__(self, output_size):
            super(EvalTestModel, self).__init__()
            self.custom_fn = EvalTestFunction.apply
            self.bias = Parameter(torch.empty(
                output_size,
                device=torch.cuda.current_device(),
                dtype=torch.float))

            # Always initialize bias to zero.
            with torch.no_grad():
                self.bias.uniform_()

        def forward(self, model_input):
            # model_input did not require_grad
            out = self.custom_fn(model_input)
            return out + self.bias

    output_size = 1024

    def model_builder():
        return EvalTestModel(output_size)

    def input_generator():
        return torch.randn(output_size, dtype=torch.float)

    # generate a label that have same shape as forward output.
    label_input = torch.ones([output_size])

    # Test pure inferencing scenarios, when inputs don't requires_grad.
    run_evaluate_test_and_compare(model_builder, input_generator, label_input)


def test_TwoOutputFunction():
    class TwoOutputFunction(torch.autograd.Function):
        @staticmethod
        # bias is an optional argument
        def forward(ctx, x, y):
            ctx.save_for_backward(x, y)
            w = x + y
            z = x * y
            return w, z

        @staticmethod
        def backward(ctx, dw, dz):
            x, y = ctx.saved_tensors
            # dL/dx = dL/dw * dw/dx + dL/dz * dz/dx
            # dw/dx = 1
            # dz/dx = y
            dx = dw * 1.0 + dz * y
            #
            # dL/dw = dL/dw * dw/dy + dL/dz * dz/dy
            # dw/dy = 1
            # dz/dy = x
            dy = dw * 1.0 + dz * x
            return dx, dy

    class TwoOutputModel(torch.nn.Module):
        def __init__(self, output_size):
            super(TwoOutputModel, self).__init__()
            self.fun = TwoOutputFunction.apply
            self.bias = Parameter(torch.empty(
                output_size,
                device=torch.cuda.current_device(),
                dtype=torch.float))

            # Always initialize bias to zero.
            with torch.no_grad():
                self.bias.uniform_()

        def forward(self, x):
            a, b = self.fun(x, self.bias)
            return a + b

    output_size = 2

    def model_builder():
        return TwoOutputModel(output_size)

    def input_generator():
        return torch.randn(output_size, dtype=torch.float)

    # generate a label that have same shape as forward output.
    label_input = torch.ones([output_size])

    # Test multi-input and multi-output custom function.
    run_training_test_and_compare(model_builder, input_generator, label_input)


def test_InnerModuleCall():
    class InnerModel(torch.nn.Module):
        def __init__(self, dim, device):
            super(InnerModel, self).__init__()
            self.bias = Parameter(torch.FloatTensor([1.0] * dim).to(device))

        def forward(self, x):
            z = 0.5 * x * x + self.bias
            return z

    class OuterFunction(torch.autograd.Function):
        @staticmethod
        def forward(ctx, x, dim, device, use_ort):
            ctx.save_for_backward(x)
            ctx.device = device
            ctx.inner = InnerModel(dim, device).to(device)
            if use_ort:
                ctx.inner = ORTModule(ctx.inner)
                set_onnx_fallthrough_export_type(ctx.inner)
            z = ctx.inner(x)
            return z

        @staticmethod
        def backward(ctx, dv):
            x, = ctx.saved_tensors
            y = x.detach().to(ctx.device)
            y.requires_grad = True
            g = None
            with torch.enable_grad():
                z = ctx.inner(y)
            z.backward(dv)
            g = y.grad.detach()
            return g, None, None, None

    class OuterModel(torch.nn.Module):
        def __init__(self, dim, device, use_ort):
            super(OuterModel, self).__init__()
            self.fun = OuterFunction.apply
            self.dim = dim
            self.device = device
            self.use_ort = use_ort
            self.bias = Parameter(torch.FloatTensor([1.0] * dim).to(device))

            # Always initialize bias to zero.
            with torch.no_grad():
                self.bias.uniform_()

        def forward(self, x):
            z = self.fun(x + self.bias, self.dim, self.device, self.use_ort)
            return z

    def get_inner_module_call_result(x, device, use_ort):
        torch.manual_seed(0)
        x = x.to(device)
        x.requires_grad = True
        model = OuterModel(2, device, use_ort)
        y = model(x).sum()
        y.backward()
        return y.detach(), x.grad.detach()

    x = torch.FloatTensor([1.0, -1.0])

    # Test indirect ORTModule call from custom function
    result_pth = get_inner_module_call_result(x.detach(), 'cuda:0', False)
    result_ort = get_inner_module_call_result(x.detach(), 'cuda:0', True)
    compare_tensor_list(result_ort, result_pth)

    # Test indirect ORTModule call from custom function
    result_ort = get_inner_module_call_result(x.detach(), 'cpu', True)
    result_pth = get_inner_module_call_result(x.detach(), 'cpu', False)
    compare_tensor_list(result_ort, result_pth)
