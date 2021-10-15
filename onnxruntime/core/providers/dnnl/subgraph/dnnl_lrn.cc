// Copyright(C) 2021 Intel Corporation
// Licensed under the MIT License

#include "dnnl_lrn.h"
#include "dnnl_subgraph.h"
#include "dnnl_subgraph_primitive.h"

namespace onnxruntime {
namespace ort_dnnl {

DnnlLrn::DnnlLrn() {}

// assume all dims are available
void DnnlLrn::CreatePrimitive(DnnlSubgraphPrimitive& sp, DnnlNode& node) {
  using namespace dnnl;

  // get the engine, currently only support either single gpu or single cpu device
  auto dnnl_engine = sp.GetEngine();

  auto alpha = ReadAlpha(node);
  auto beta = ReadBeta(node);
  auto bias = ReadBias(node);
  auto size = ReadSize(node);

  auto lrn_src_mem = sp.GetMemory(node.Input(IN_X));
  auto lrn_src_md = lrn_src_mem.get_desc();

  auto lrn_desc = dnnl::lrn_forward::desc(dnnl::prop_kind::forward_scoring, dnnl::algorithm::lrn_across_channels, lrn_src_md, size, alpha, beta, bias);
  auto lrn_pd = dnnl::lrn_forward::primitive_desc(lrn_desc, dnnl_engine);

  // If using GPU this will move the memory from the CPU to the GPU.
  lrn_src_mem = sp.GetMemoryAndReshape(node.Input(IN_X), lrn_pd.src_desc(), dnnl_engine);
  auto lrn_dst_mem = dnnl::memory(lrn_pd.dst_desc(), dnnl_engine);

  auto lrn_op = dnnl::lrn_forward(lrn_pd);
  sp.AddPrimitive(lrn_op, {{DNNL_ARG_SRC, lrn_src_mem},
                       {DNNL_ARG_DST, lrn_dst_mem}});

  sp.SetMemory(node.Output(OUT_Y), lrn_dst_mem);
}

int64_t DnnlLrn::ReadSize(DnnlNode& node) {
  auto attr = node.Attributes().find("size");
  int64_t size = 0;
  if (attr != node.Attributes().end() &&
      attr->second().type() == ::ONNX_NAMESPACE::AttributeProto_AttributeType::AttributeProto_AttributeType_INT) {
    size = attr->second().i();
  }
  ORT_ENFORCE(size > 0);
  ORT_ENFORCE(size % 2 == 1);
  return size;
}

float DnnlLrn::ReadAlpha(DnnlNode& node) {
  auto attr = node.Attributes().find("alpha");
  float alpha = 0;
  if (attr != node.Attributes().end() &&
      attr->second().type() == ::ONNX_NAMESPACE::AttributeProto_AttributeType::AttributeProto_AttributeType_FLOAT) {
    alpha = attr->second().f();
  }
  return alpha;
}

float DnnlLrn::ReadBeta(DnnlNode& node) {
  auto attr = node.Attributes().find("beta");
  float beta = 0;
  if (attr != node.Attributes().end() &&
      attr->second().type() == ::ONNX_NAMESPACE::AttributeProto_AttributeType::AttributeProto_AttributeType_FLOAT) {
    beta = attr->second().f();
  }
  return beta;
}

float DnnlLrn::ReadBias(DnnlNode& node) {
  auto attr = node.Attributes().find("bias");
  float bias = 1.0f;
  if (attr != node.Attributes().end() &&
      attr->second().type() == ::ONNX_NAMESPACE::AttributeProto_AttributeType::AttributeProto_AttributeType_FLOAT) {
    bias = attr->second().f();
  }
  return bias;
}

}  // namespace ort_dnnl
}  // namespace onnxruntime
