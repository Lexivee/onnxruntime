FROM rocm/pytorch:rocm4.3.1_ubuntu18.04_py3.6_pytorch_1.9.0

WORKDIR /stage

# rocm-ci branch contains instrumentation needed for loss curves and perf
RUN git clone https://github.com/microsoft/huggingface-transformers.git &&\
      cd huggingface-transformers &&\
      git checkout rocm-ci &&\
      pip install .

RUN pip install \
      numpy \
      onnx \
      cerberus \
      sympy \
      h5py \
      datasets==1.9.0 \
      requests \
      sacrebleu \
      sacremoses \
      scipy \
      scikit-learn \
      sklearn \
      tokenizers \
      sentencepiece
