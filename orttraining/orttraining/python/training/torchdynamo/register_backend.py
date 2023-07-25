# -------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT License.
# --------------------------------------------------------------------------

from functorch.compile import min_cut_rematerialization_partition
from torch._dynamo.backends.common import aot_autograd
from torch.onnx._internal.exporter import ExportOptions

from .ort_backend import OrtBackend


def make_aot_ort(dynamic: bool = True):
    """Wrap OrtBackend as PyTorch's AOT compiler.

    Example usages:
        import torch
        from onnxruntime.training.torchdynamo.register_backend import make_aot_ort
        use_dynamic = True
        local_aot_ort, _ = make_aot_ort(dynamic = use_dynamic)

        @torch._dynamo.optimize(local_aot_ort, dynamic=use_dynamic)
        def foo(x: torch.Tensor):
            return torch.sigmoid(x)

        x = torch.rand(2, 2, dtype=torch.float)
        torch.testing.assert_close(torch.sigmoid(x), foo(x))
    """
    ort_backend = OrtBackend(onnx_exporter_options=ExportOptions(dynamic_shapes=dynamic))
    return (
        aot_autograd(
            fw_compiler=ort_backend,
            partition_fn=min_cut_rematerialization_partition,
            decompositions=ort_backend.resolved_onnx_exporter_options.decomposition_table,
        ),
        ort_backend,
    )


# Wrap ORT as a compiler in Dynamo for training (i.e., when .backward is called).
#
# Under the hood, OrtBackend.compile is called inside functorch. See aot_function
# and aot_module in aot_autograd.py in PyTorch repo for more details. Basically,
# OrtBackend.compile is mapped to forward graph compiler, fw_compile, and backward
# graph compiler, bw_compile, in aot_autograd.py.
#
# Example usage:
#  import torch
#  from onnxruntime.training.torchdynamo.register_backend import aot_ort
#  model = torch.nn.Linear(2, 2)
#  compiled_model = torch._dynamo.optimize(aot_ort)(model)
#  result = compiled_model(torch.rand(2, 2, dtype=torch.float)
#  result.sum().backward()
#
# DEFAULT_BACKEND should be the underlying compiler for ALL graphs if
# the user uses ORT to accelerate PyTorch via Dynamo.
# By using a global compiler for all graphs, cached compilation
# results can be reused when encountering the identical graphs.
aot_ort, DEFAULT_BACKEND = make_aot_ort(dynamic=False)

# Similar to aot_ort but should be used with
#    torch._dynamo.optimize(dynamic_aot_ort, dynamic=True)
# to enable dynamic shapes in ONNX graph.
#
# Similar to DEFAULT_BACKEND but DEFAULT_DYNAMIC_BACKEND enables dynamic shapes
# when exporting FX graph to ONNX.
# Note that this backend must be used with
#    torch._dynamo.optimize(DEFAULT_DYNAMIC_BACKEND, dynamic=True)
# Without `dynamic=True`, the FX graph only contains static shapes, and results ONNX graph
# with static shapes.
dynamic_aot_ort, DEFAULT_DYNAMIC_BACKEND = make_aot_ort(dynamic=True)

# Declare ORT as a compiler in Dynamo for inference (i.e., when .backward is NOT called).
#
# ort is usually faster than aot_ort for inference because the graphs generated by aot_autograd
# mechanism are very different than the original graphs. Therefore, some ORT's graph transformers
# are not applicable.
#
# Example usage:
#  import torch
#  from onnxruntime.training.torchdynamo.register_backend import ort
#  model = torch.nn.Linear(2, 2)
#  compiled_model = torch._dynamo.optimize(ort)(model)
ort = DEFAULT_BACKEND

# Similar to ort but should be used with
#    torch._dynamo.optimize(dynamic_ort, dynamic=True)
# to enable dynamic shapes in ONNX graph.
dynamic_ort = DEFAULT_DYNAMIC_BACKEND
