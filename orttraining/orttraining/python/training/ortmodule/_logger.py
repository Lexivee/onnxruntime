# -------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT License.
# --------------------------------------------------------------------------

import logging
import os
import sys
import tempfile
import time
from contextlib import contextmanager
from enum import IntEnum
from typing import Callable, Dict, List, Optional

from onnxruntime.capi._pybind_state import Severity

from ._utils import get_rank


class LogLevel(IntEnum):
    VERBOSE = 0
    INFO = 1
    WARNING = 2
    ERROR = 3
    FATAL = 4


@contextmanager
def suppress_os_stream_output(on_exit: Callable = None):
    """Suppress output from being printed to stdout and stderr.

    If on_exit is not None, it will be called when the context manager exits.
    """

    # stdout and stderr is written to a tempfile instead
    _, fo_file_path = tempfile.mkstemp()

    # store original stdout and stderr file no.
    old_stdout = os.dup(sys.stdout.fileno())
    old_stderr = os.dup(sys.stderr.fileno())

    # Allow read and write the file.
    fd = os.open(fo_file_path, os.O_RDWR | os.O_CREAT)
    fo = os.fdopen(fd)

    try:
        # Redirect stdout and stderr (printed from Python or C++) to the file.
        os.dup2(fo.fileno(), sys.stdout.fileno())
        os.dup2(fo.fileno(), sys.stderr.fileno())
        yield
    finally:
        sys.stdout.flush()
        sys.stderr.flush()

        # Restore stdout and stderr.
        os.dup2(old_stdout, sys.stdout.fileno())
        os.dup2(old_stderr, sys.stderr.fileno())

        if on_exit:
            on_exit(fo)

        fo.close()


def create_log_filter(logger: logging.LoggerAdapter, record_filters: Optional[List[str]]):
    def _log_with_filter(fo):
        if fo.tell() > 0:
            if logger.logger.disabled:
                return
            # Filter out the error message
            fo.seek(0)
            suppress_output_messages = fo.readlines()
            if record_filters:
                filtered_messages = []
                for i in range(len(suppress_output_messages)):
                    found = False
                    for warning in record_filters:
                        if warning in suppress_output_messages[i]:
                            found = True
                            break

                    if not found:
                        filtered_messages.append(suppress_output_messages[i])
                logger.warning("".join(filtered_messages))
            else:
                logger.warning("".join(suppress_output_messages))

    return _log_with_filter


ORTMODULE_LOG_LEVEL_MAP: Dict[LogLevel, List[int]] = {
    LogLevel.VERBOSE: [Severity.VERBOSE, logging.DEBUG],
    LogLevel.INFO: [Severity.INFO, logging.INFO],
    LogLevel.WARNING: [Severity.WARNING, logging.WARNING],
    LogLevel.ERROR: [Severity.ERROR, logging.ERROR],
    LogLevel.FATAL: [Severity.FATAL, logging.FATAL],
}


def ortmodule_loglevel_to_onnxruntime_c_loglevel(loglevel: LogLevel) -> int:
    return ORTMODULE_LOG_LEVEL_MAP.get(loglevel, [Severity.WARNING, logging.WARNING])[0]


def ortmodule_loglevel_to_python_loglevel(loglevel: LogLevel) -> int:
    return ORTMODULE_LOG_LEVEL_MAP.get(loglevel, [Severity.WARNING, logging.WARNING])[1]


def configure_ortmodule_logger(logger: logging.Logger, log_level: LogLevel) -> logging.LoggerAdapter:
    extra = {"rank": get_rank()}
    syslog = logging.StreamHandler(stream=sys.stdout)
    formatter = logging.Formatter(
        "[RANK %(rank)s] [%(asctime)s] [%(levelname)s] [%(filename)s:%(lineno)d:%(funcName)s] %(message)s"
    )
    syslog.setFormatter(formatter)
    logger.addHandler(syslog)
    logger.propagate = False
    # Disable the logger for non-zero ranks when level > info
    logger.disabled = log_level > LogLevel.INFO and get_rank() != 0
    logger.setLevel(ortmodule_loglevel_to_python_loglevel(log_level))
    return logging.LoggerAdapter(logger, extra)


class LogColor:
    HEADER = "\033[95m"
    BLUE = "\033[94m"
    CYAN = "\033[96m"
    GREEN = "\033[92m"
    WARNING = "\033[93m"
    RED = "\033[91m"
    ENDC = "\033[0m"
    BOLD = "\033[1m"
    UNDERLINE = "\033[4m"


class TimeTrackerPhase(IntEnum):
    EndToEnd = 0  # The total overhead of ORT first-time initialization
    EXPORT = 1  # The latency of preparing and exporting the model to ONNX
    GRAPH_BUILDER_INIT = 2  # The latency of initializing the graph builder
    DETECTION = 3  # The latency of runtime detection
    BUILD_GRAPH = 4  # The latency of optimizing forward graph (and building the gradient graph for training).
    CREATE_SESSION = 5  # The latency of creating the session

    def to_string(self) -> str:
        if self == TimeTrackerPhase.EndToEnd:
            return "end to end"
        if self == TimeTrackerPhase.EXPORT:
            return "export"
        elif self == TimeTrackerPhase.GRAPH_BUILDER_INIT:
            return "graph builder init"
        elif self == TimeTrackerPhase.DETECTION:
            return "runtime detection"
        elif self == TimeTrackerPhase.BUILD_GRAPH:
            return "graph building"
        elif self == TimeTrackerPhase.CREATE_SESSION:
            return "session creation"
        else:
            return "invalid"


class TimeTracker:
    """A simple class to track time spent in different phases of ORT backend first-time initialization."""

    NOT_RECORD = -1.0

    def __init__(
        self,
    ):
        self.starts_: List[float] = [TimeTracker.NOT_RECORD] * len(TimeTrackerPhase)
        self.ends_: List[float] = [TimeTracker.NOT_RECORD] * len(TimeTrackerPhase)

    def start(self, phase: TimeTrackerPhase):
        self.starts_[phase] = time.time()

    def end(self, phase: TimeTrackerPhase):
        self.ends_[phase] = time.time()

    def _get_duration(self, phase: TimeTrackerPhase):
        if self.ends_[phase] == TimeTracker.NOT_RECORD or self.starts_[phase] == TimeTracker.NOT_RECORD:
            return TimeTracker.NOT_RECORD
        return self.ends_[phase] - self.starts_[phase]

    def to_string(self, log_details=False) -> str:
        end_to_end_str = self._get_duration(TimeTrackerPhase.EndToEnd)
        end_to_end_str = f"{end_to_end_str:.2f}" if end_to_end_str != TimeTracker.NOT_RECORD else "N/A"
        export_str = self._get_duration(TimeTrackerPhase.EXPORT)
        export_str = f"{export_str:.2f}" if export_str != TimeTracker.NOT_RECORD else "N/A"
        overhead_title_str = (
            f"Total ORT initialization overhead is {end_to_end_str}s where export takes {export_str}s.\n"
        )

        if log_details is False:
            return overhead_title_str

        duration_summaries = []
        for phase in TimeTrackerPhase:
            _get_duration = self._get_duration(phase)
            if phase in [TimeTrackerPhase.EndToEnd, TimeTrackerPhase.EXPORT]:
                continue

            val = (
                f" {phase.to_string()} takes {_get_duration:.2f}s" if _get_duration != TimeTracker.NOT_RECORD else "N/A"
            )
            duration_summaries.append(f"{val}")

        return f"{overhead_title_str}Other overhead details: {','.join(duration_summaries)}\n"


class TrackTime:
    """A function decorator to track time spent in different phases of ORT backend first-time initialization."""

    def __init__(self, phase: TimeTrackerPhase):
        self.phase = phase

    def __call__(self, func: Callable):
        def wrapper(graph_execution_manager, *args, **kwargs):
            if not hasattr(graph_execution_manager, "time_tracker"):
                raise RuntimeError("The class of the function to be tracked must have a 'time_tracker' attribute.")
            graph_execution_manager.time_tracker.start(self.phase)
            result = func(graph_execution_manager, *args, **kwargs)
            graph_execution_manager.time_tracker.end(self.phase)
            return result

        return wrapper
