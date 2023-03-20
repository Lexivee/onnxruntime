# -------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT License.
# --------------------------------------------------------------------------

from pathlib import Path

import os
import warnings
import shutil
import torch

from ._subscriber_manager import ModuleHookSubscriberBase


class StatisticsSubscriber(ModuleHookSubscriberBase):
    """
    This subscriber is used to dump the activation's statistics to a file.

    Each activation will be summarized into 1 or 2 files, depending on whether it is used in the backward pass.
    So for each run step, there will be many files.
    > In the forward pass, summarize the tensor's statistics and write to a file.
    > In the backward pass, summarize the tensor's gradident's statistics and write into another file.

    Currently the statistics mainly include:
    > Number of inf/nan values.
    > Common statistics: tensor shape, data type, total element size, min/max/mean/std of the tensor elements.
    > Number of zero/negative/positive elements.
    > Sampled elements (taking the first 128 elements for example).
    > To be extended...

    `merge_activation_summary.py` can be used to merge the files into one file per training step.
    """

    def __init__(
        self, output_dir: str, start_step: int = 0, end_step: int = 1000000, override_output_dir: bool = False
    ):
        """
        Steps in [start_step, end_step) will run subscriber actions.
        :param output_dir: the directory all activation statistics files will be stored.
        :param start_step: the first step that runs subscriber actions.
        :param end_step: the end step (exclusively) that runs subscriber actions.
        :param override_output_dir: whether `output_dir` can be overriden if it already exists.
        """
        super().__init__(start_step=start_step, end_step=end_step)
        self._output_dir = output_dir
        if os.path.exists(self._output_dir):
            if override_output_dir:

                warnings.warn(f"Output directory {self._output_dir} already exists, overriding it.")
                shutil.rmtree(self._output_dir)
            else:
                raise FileExistsError(
                    f"Output directory {self._output_dir} already exists. "
                    "Set override_output_dir=True for StatisticsSubscriber if this is the intention."
                )

    def module_post_forward_impl(self, activation: torch.Tensor, depth: int, name: str, step: int):
        output_file_path = os.path.join(f"{self._output_dir}", f"step_{step}")
        return self._summarize_activations(activation, depth, name, output_file_path, True)

    def module_pre_backward_impl(self, activation: torch.Tensor, depth: int, name: str, step: int):
        output_file_path = os.path.join(f"{self._output_dir}", f"step_{step}")
        return self._summarize_activations(activation, depth, name, output_file_path, False)

    def _summarize_activations(self, tensor: torch.Tensor, depth: int, name: str, step_folder: str, is_forward: bool):
        display_name = name + " forward run" if is_forward is True else name + " backward run"
        output_file_name = name + "_forward" if is_forward is True else name + "_backward"

        if tensor is None or not isinstance(tensor, torch.Tensor):
            print(f"{display_name} not a torch tensor, value: {tensor}")
            return

        step_path = Path(step_folder)
        if not step_path.exists():
            step_path.mkdir(parents=True, exist_ok=False)
        order_file_path = step_path / "order.txt"
        tensor_file_path = step_path / output_file_name

        # This is to try best effort to align the count of number per line for easier comparison in diff view,
        # though it does not always guarantee doing this way.
        torch.set_printoptions(precision=6, linewidth=128)

        flatten_array = tensor.flatten()
        zerotensor = torch.tensor(0, dtype=flatten_array.dtype, device=flatten_array.device)
        num_nan = torch.isnan(flatten_array).sum()
        num_inf = torch.isinf(flatten_array).sum()

        with order_file_path.open(mode="a", encoding="utf-8") as f:
            f.write(f"{output_file_name}\n")

        with tensor_file_path.open(mode="w", encoding="utf-8") as f:
            f.write(
                f"{'>'*depth + display_name} shape: {tensor.shape} dtype: {tensor.dtype} size: {flatten_array.size()} \n"
                f"min: {flatten_array.min()} max: {flatten_array.max()}, mean: {flatten_array.mean()}, "
                f"std: {flatten_array.std()} \n"
                f"nan: {num_nan}, inf: {num_inf}\n"
            )
            f.write(f"samples(top 128): {flatten_array[:128]}\n")

            f.write(
                f"neg: {torch.less(flatten_array, zerotensor).to(torch.int64).sum()}, "
                f"pos: {torch.greater(flatten_array, zerotensor).to(torch.int64).sum()}, "
                f"zero: {torch.eq(flatten_array, zerotensor).to(torch.int64).sum()},\n"
            )
            f.write(f"{'='*16}\n")
