# -------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT License.
# --------------------------------------------------------------------------

import os
import sys
import warnings

import torch
from packaging import version

from onnxruntime import set_seed
from onnxruntime.capi import build_and_package_info as ort_info
from onnxruntime.capi._pybind_state import is_ortmodule_available

from ._fallback import ORTModuleFallbackException, ORTModuleInitException, _FallbackPolicy, wrap_exception
from .options import _MemoryOptimizationLevel
from .torch_cpp_extensions import is_installed as is_torch_cpp_extensions_installed

if not is_ortmodule_available():
    raise ImportError("ORTModule is not supported on this platform.")


def _defined_from_envvar(name, default_value, warn=True):
    new_value = os.getenv(name, None)
    if new_value is None:
        return default_value
    try:
        new_value = type(default_value)(new_value)
    except (TypeError, ValueError) as e:
        if warn:
            warnings.warn(f"Unable to overwrite constant {name!r} due to {e!r}.")
        return default_value
    return new_value


################################################################################
# All global constant goes here, before ORTModule is imported ##################
# NOTE: To *change* values in runtime, import onnxruntime.training.ortmodule and
# assign them new values. Importing them directly do not propagate changes.
################################################################################
ONNX_OPSET_VERSION = 17
MINIMUM_RUNTIME_PYTORCH_VERSION_STR = "1.8.1"
ORTMODULE_TORCH_CPP_DIR = os.path.join(os.path.dirname(__file__), "torch_cpp_extensions")
_FALLBACK_INIT_EXCEPTION = None
ORTMODULE_FALLBACK_POLICY = (
    _FallbackPolicy.FALLBACK_UNSUPPORTED_DEVICE
    | _FallbackPolicy.FALLBACK_UNSUPPORTED_DATA
    | _FallbackPolicy.FALLBACK_UNSUPPORTED_TORCH_MODEL
    | _FallbackPolicy.FALLBACK_UNSUPPORTED_ONNX_MODEL
)
ORTMODULE_FALLBACK_RETRY = False
ORTMODULE_IS_DETERMINISTIC = torch.are_deterministic_algorithms_enabled()

ONNXRUNTIME_CUDA_VERSION = ort_info.cuda_version if hasattr(ort_info, "cuda_version") else None
ONNXRUNTIME_ROCM_VERSION = ort_info.rocm_version if hasattr(ort_info, "rocm_version") else None

# The first value indicates whether the code is in ONNX export context.
# The second value is the memory optimization level to be used after ONNX export.
ORTMODULE_ONNX_EXPORT_CONTEXT = [False, _MemoryOptimizationLevel.USER_SPECIFIED]


# Verify minimum PyTorch version is installed before proceeding to ONNX Runtime initialization
try:
    import torch

    runtime_pytorch_version = version.parse(torch.__version__.split("+")[0])
    minimum_runtime_pytorch_version = version.parse(MINIMUM_RUNTIME_PYTORCH_VERSION_STR)
    if runtime_pytorch_version < minimum_runtime_pytorch_version:
        raise wrap_exception(
            ORTModuleInitException,
            RuntimeError(
                "ONNX Runtime ORTModule frontend requires PyTorch version greater"
                f" or equal to {MINIMUM_RUNTIME_PYTORCH_VERSION_STR},"
                f" but version {torch.__version__} was found instead."
            ),
        )

    # Try best effort to override the checkpoint function during ONNX model export.
    from torch.utils.checkpoint import checkpoint as original_torch_checkpoint

    def _checkpoint(
        function,
        *args,
        use_reentrant=None,
        context_fn=torch.utils.checkpoint.noop_context_fn,
        determinism_check=torch.utils.checkpoint._DEFAULT_DETERMINISM_MODE,
        debug=False,
        **kwargs,
    ):
        if ORTMODULE_ONNX_EXPORT_CONTEXT[0] is True:
            # Automatically activate layer-specific memory optimization if the checkpoint function
            # is detected.
            # > Observation 1: The employment of the checkpoint function typically suggests that the
            #   model's size exceeds the GPU's memory capacity.
            #   However, it's possible that some user models apply the checkpoint function for gradient
            #   checkpointing that isn't layer-specific, though this is believed to be rare.
            # > Observation 2: Conversely, failing to modify the checkpoint function here will likely
            #   result in unsuccessful ONNX exports.
            ORTMODULE_ONNX_EXPORT_CONTEXT[1] = _MemoryOptimizationLevel.TRANSFORMER_LAYERWISE_RECOMPUTE
            print(
                "Layer-wise memory optimization is enabled automatically upon detecting "
                "torch.utils.checkpoint usage during model execution."
            )
            return function(*args, **kwargs)
        return original_torch_checkpoint(
            function,
            *args,
            use_reentrant=use_reentrant,
            context_fn=context_fn,
            determinism_check=determinism_check,
            debug=debug,
            **kwargs,
        )

    torch.utils.checkpoint.checkpoint = _checkpoint


except ORTModuleFallbackException as e:
    # Initialization fallback is handled at ORTModule.__init__
    _FALLBACK_INIT_EXCEPTION = e
except ImportError as e:
    raise RuntimeError(
        f"PyTorch {MINIMUM_RUNTIME_PYTORCH_VERSION_STR} must be "
        "installed in order to run ONNX Runtime ORTModule frontend!"
    ) from e

# Verify whether PyTorch C++ extensions are already compiled
# TODO: detect when installed extensions are outdated and need reinstallation. Hash? Version file?
if not is_torch_cpp_extensions_installed(ORTMODULE_TORCH_CPP_DIR) and "-m" not in sys.argv:
    _FALLBACK_INIT_EXCEPTION = wrap_exception(
        ORTModuleInitException,
        RuntimeError(
            f"ORTModule's extensions were not detected at '{ORTMODULE_TORCH_CPP_DIR}' folder. "
            "Run `python -m torch_ort.configure` before using `ORTModule` frontend."
        ),
    )

# Initalized ORT's random seed with pytorch's initial seed
# in case user has set pytorch seed before importing ORTModule
set_seed(torch.initial_seed() % sys.maxsize)


# Override torch.manual_seed and torch.cuda.manual_seed
def override_torch_manual_seed(seed):
    set_seed(int(seed % sys.maxsize))
    return torch_manual_seed(seed)


torch_manual_seed = torch.manual_seed
torch.manual_seed = override_torch_manual_seed


def override_torch_cuda_manual_seed(seed):
    set_seed(int(seed % sys.maxsize))
    return torch_cuda_manual_seed(seed)


torch_cuda_manual_seed = torch.cuda.manual_seed
torch.cuda.manual_seed = override_torch_cuda_manual_seed


def _use_deterministic_algorithms(enabled):
    global ORTMODULE_IS_DETERMINISTIC  # noqa: PLW0603
    ORTMODULE_IS_DETERMINISTIC = enabled


def _are_deterministic_algorithms_enabled():
    global ORTMODULE_IS_DETERMINISTIC  # noqa: PLW0602
    return ORTMODULE_IS_DETERMINISTIC


from .graph_optimizer_registry import register_graph_optimizer  # noqa: E402, F401
from .graph_optimizers import *  # noqa: E402, F403
from .options import DebugOptions, LogLevel  # noqa: E402, F401

# ORTModule must be loaded only after all validation passes
from .ortmodule import ORTModule  # noqa: E402, F401
