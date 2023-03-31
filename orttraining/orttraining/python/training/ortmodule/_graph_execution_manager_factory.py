# -------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT License.
# --------------------------------------------------------------------------

from ._fallback import _FallbackManager
from ._inference_manager import InferenceManager
from ._training_manager import TrainingManager
from .debug_options import DebugOptions


class GraphExecutionManagerFactory:
    def __init__(self, module, debug_options: DebugOptions, fallback_manager: _FallbackManager):
        self._training_manager = TrainingManager(module, debug_options, fallback_manager)
        self._inference_manager = InferenceManager(module, debug_options, fallback_manager)

    def __call__(self, is_training):
        if is_training:
            return self._training_manager
        else:
            return self._inference_manager
