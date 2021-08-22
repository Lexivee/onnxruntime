import os
import subprocess
from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
from subprocess import CalledProcessError
import pybind11
import onnx
from distutils.sysconfig import get_python_lib
class CMakeExtension(Extension):
    def __init__(self, name, sourcedir=""):
        Extension.__init__(self, name, sources=[])
        self.sourcedir = os.path.abspath(sourcedir)

class CMakeBuild(build_ext):
    def build_extension(self, ext):
        extdir = os.path.abspath(os.path.dirname(self.get_ext_fullpath(ext.name)))

        if not os.path.exists(self.build_temp):
            os.makedirs(self.build_temp)
        subprocess.check_call(
            ["cmake",
             "-Dpybind11_DIR={}".format(pybind11.get_cmake_dir()),
             "-DONNX_INCLUDE_DIR={}".format(os.path.dirname(os.path.dirname(onnx.__file__))),
             "-DPYTHON_SITE_PACKAGES={}".format(get_python_lib()),
             "-DCMAKE_LIBRARY_OUTPUT_DIRECTORY={}".format(extdir),
             ext.sourcedir], cwd=self.build_temp)
        subprocess.check_call(
            ["cmake", "--build", ".", "-v"], cwd=self.build_temp
        )

setup(name='orttraining_external_custom_ops',
    version='0.1',
    author='',
    author_email='',
    description='External custom ops example',
    long_description='',
    ext_modules=[CMakeExtension('orttrainng_external_custom_ops')],
    cmdclass=dict(build_ext=CMakeBuild),
    zip_safe=False
)
