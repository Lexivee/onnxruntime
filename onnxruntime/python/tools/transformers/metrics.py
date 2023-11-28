# -------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation.  All rights reserved.
# Licensed under the MIT License.  See License.txt in the project root for
# license information.
# --------------------------------------------------------------------------

import datetime
import hashlib
import json
import pandas as pd

from abc import ABC

class BaseObject(ABC):
    def __init__(self):
        self.customized = {}

    def to_dict(self):
        default_values = self.__dict__.copy()
        default_values.pop('customized', None)
        default_values.update(self.customized)
        return {k: v for k, v in default_values.items() if v}

class ModelInfo(BaseObject):
    def __init__(self,
                 full_name: str = None,
                 is_huggingface: bool = False,
                 is_text_generation: bool = False,
                 short_name: str = None):
        super().__init__()
        self.full_name = full_name
        self.is_huggingface = is_huggingface
        self.is_text_generation = is_text_generation
        self.short_name = short_name
        self.input_shape = []

class Config(BaseObject):
    def __init__(self,
                    backend: str = "onnxruntime",
                    batch_size: int = 1,
                    seq_length: int = 0,
                    precision: str = "fp32",
                    warmup_runs: int = 1,
                    measured_runs: int = 10):
        super().__init__()
        self.backend = backend
        self.batch_size = batch_size
        self.seq_length = seq_length
        self.precision = precision
        self.warmup_runs = warmup_runs
        self.measured_runs = measured_runs

class BackendOptions(BaseObject):
    def __init__(self,
                 enable_profiling: bool = False,
                 execution_provider: str = None,
                 use_io_binding: bool = False):
        super().__init__()
        self.enable_profiling = enable_profiling
        self.execution_provider = execution_provider
        self.use_io_binding = use_io_binding

class Metadata(BaseObject):
    def __init__(self,
                 device: str = None,
                 package_name: str = None,
                 package_version: str = None,
                 platform: str = None,
                 python_version: str = None):
        super().__init__()
        self.device = device
        self.package_name = package_name
        self.package_version = package_version
        self.platform = platform
        self.python_version = python_version

class Metrics(BaseObject):
    def __init__(self,
                 avg_run_latency_ms: float = 0.0,
                 throughput_qps: float = 0.0,
                 max_memory_usage_GB: float = 0.0):
        super().__init__()
        self.avg_run_latency_ms = avg_run_latency_ms
        self.throughput_qps = throughput_qps
        self.max_memory_usage_GB = max_memory_usage_GB

class BenchmarkRecord:
    def __init__(self,
                 model_name: str,
                 precision: str,
                 backend: str,
                 device: str,
                 package_name: str,
                 package_version: str,
                 batch_size: int = 1,
                 warmup_runs: int = 1,
                 measured_runs: int = 10,
                 trigger_date: str = None):
        self.config = Config()
        self.metrics = Metrics()
        self.model_info = ModelInfo()
        self.metadata = Metadata()
        self.backend_options = BackendOptions()
        self.trigger_date = trigger_date or datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        self.model_info.full_name = model_name
        self.config.precision = precision
        self.config.backend = backend
        self.config.batch_size = batch_size
        self.config.warmup_runs = warmup_runs
        self.config.measured_runs = measured_runs
        self.metadata.device = device
        self.metadata.package_name = package_name
        self.metadata.package_version = package_version

    def __calculate_hash(self, input: str) -> str:
        hash_object = hashlib.md5()
        hash_object.update(input.encode("utf-8"))
        return hash_object.hexdigest()

    def to_dict(self) -> dict:
        self.config_hash = self.__calculate_hash(json.dumps(self.config.to_dict(), default=str))
        self.run_hash = self.__calculate_hash(json.dumps(self.config.to_dict(), default=str) + json.dumps(self.metadata.to_dict(), default=str))

        if self.model_info:
            if self.model_info.full_name:
                self.model_full_name = self.model_info.full_name
            elif self.model_info.short_name:
                self.model_full_name = self.model_info.short_name
            else:
                raise ValueError("model_info.full_name and model_info.short_name cannot be both empty")
        
        self.backend = self.config.backend
        self.batch_size = self.config.batch_size
        self.seq_length = self.config.seq_length
        self.precision = self.config.precision
        self.device = self.metadata.device

        return {
            'model_info': self.model_info.to_dict(),
            'config': self.config.to_dict(),
            'backend_options': self.backend_options.to_dict(),
            'metadata': self.metadata.to_dict(),
            'metrics': self.metrics.to_dict(),
            'trigger_date': self.trigger_date
        }

    def to_json(self) -> str:
        return json.dumps(self.to_dict(), default=str)


    @classmethod
    def save_as_csv(self, file_name: str, records: list = None) -> None:
        if records is None or len(records) == 0:
            return
        rds = [record.to_dict() for record in records]
        df = pd.json_normalize(rds)
        df.to_csv(file_name, index=False)
