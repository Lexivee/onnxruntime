# -------------------------------------------------------------------------
# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the MIT License.
# --------------------------------------------------------------------------
import warnings
import ctypes
import sys


def find_cudart_versions(build_env=False):
    # ctypes.CDLL and ctypes.util.find_library load the latest installed library.
    # it may not the the library that would be loaded by onnxruntime.
    # for example, in an environment already has Cuda 11.1 installed.
    # it later has conda cudatoolkit (10.2.89) installed. ctypes will find cudart 10.2.
    # however, onnxruntime will find and load Cuda 11.1 and works fine.
    # for the above reason, we need find all versions in the environment and
    # only give warnings if the expected cuda version is not found.
    # in onnxruntime build environment, we expected only one Cuda version.
    if 'linux' not in sys.platform:
        warnings.warn('find_cudart_versions only works on Linux')
        return None

    cudart_possible_versions = {None}
    if not build_env:
        # if not in a build environment, there may be more than one installed cudart.
        cudart_possible_versions.update({
            '11.3',
            '11.2',
            '11.1',
            '11.0',
            '10.2',
            '10.1',
            '10.0'})

    def get_cudart_version(find_cudart_version=None):
        cudart_lib_filename = 'libcudart.so'
        if find_cudart_version:
            cudart_lib_filename = cudart_lib_filename + '.' + find_cudart_version

        try:
            cudart = ctypes.CDLL(cudart_lib_filename)
            cudart.cudaRuntimeGetVersion.restype = int
            cudart.cudaRuntimeGetVersion.argtypes = [ctypes.POINTER(ctypes.c_int)]
            version = ctypes.c_int()
            status = cudart.cudaRuntimeGetVersion(ctypes.byref(version))
            if status != 0:
                return None
        except: # noqa
            return None

        return version.value

    # use set to avoid duplications
    cudart_found_versions = {
        get_cudart_version(find_cudart_version) for find_cudart_version in cudart_possible_versions}

    # convert to list and remove None
    return [ver for ver in cudart_found_versions if ver]


def find_cudnn_versions(build_env=False):
    # comments in get_cudart_version apply here
    if 'linux' not in sys.platform:
        warnings.warn('find_cudnn_versions only works on Linux')

    cudnn_possible_versions = {None}
    if not build_env:
        # if not in a build environment, there may be more than one installed cudnn.
        # https://developer.nvidia.com/rdp/cudnn-archive to include all that may support Cuda 10+.
        cudnn_possible_versions.update({
            '8.2',
            '8.1.1', '8.1.0',
            '8.0.5', '8.0.4', '8.0.3', '8.0.2', '8.0.1',
            '7.6.5', '7.6.4', '7.6.3', '7.6.2', '7.6.1', '7.6.0',
            '7.5.1', '7.5.0',
            '7.4.2', '7.4.1',
            '7.3.1', '7.3.0',
        })

    def get_cudnn_version(find_cudnn_version=None):
        cudnn_lib_filename = 'libcudnn.so'
        if find_cudnn_version:
            cudnn_lib_filename = cudnn_lib_filename + '.' + find_cudnn_version

        try:
            cudnn = ctypes.CDLL(cudnn_lib_filename)
            cudnn_ver = cudnn.cudnnGetVersion()
            return cudnn_ver
        except: # noqa
            return None

    # use set to avoid duplications
    cudnn_found_versions = {get_cudnn_version(find_cudnn_version) for find_cudnn_version in cudnn_possible_versions}

    # convert to list and remove None
    return list(cudnn_found_versions)
