#!/usr/bin/env python3
# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT License.

import sys
import argparse

from _test_commons import run_subprocess

import logging

logging.basicConfig(
    format="%(asctime)s %(name)s [%(levelname)s] - %(message)s",
    level=logging.DEBUG)
log = logging.getLogger("DistributedTests")

def parse_arguments():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cwd", help="Path to the current working directory")
    return parser.parse_args()

def main():
    import torch
    ngpus = torch.cuda.device_count()

    if ngpus < 2:
        raise RuntimeError("Cannot run distributed tests with less than 2 gpus.")

    args = parse_arguments()
    cwd = args.cwd

    log.info("Running distributed tests pipeline")

    # TODO: Add distributed test suite here.

    return 0


if __name__ == "__main__":
    sys.exit(main())
