---
title: ARM NN
parent: Execution Providers
grand_parent: Reference
nav_order: 2
---

## ArmNN Execution Provider

[ArmNN](https://github.com/ARM-software/armnn) is an open source inference engine maintained by Arm and Linaro companies. The integration of ArmNN as an execution provider (EP) into ONNX Runtime accelerates performance of ONNX model workloads across Armv8 cores.

### Build ArmNN execution provider
For build instructions, please see the [BUILD page](../../how-to/build.md#ArmNN).

### Using the ArmNN execution provider
#### C/C++
To use ArmNN as execution provider for inferencing, please register it as below.
```
string log_id = "Foo";
auto logging_manager = std::make_unique<LoggingManager>
(std::unique_ptr<ISink>{new CLogSink{}},
                                  static_cast<Severity>(lm_info.default_warning_level),
                                  false,
                                  LoggingManager::InstanceType::Default,
                                  &log_id)
Environment::Create(std::move(logging_manager), env)
InferenceSession session_object{so, env};
session_object.RegisterExecutionProvider(std::make_unique<::onnxruntime::ArmNNExecutionProvider>());
status = session_object.Load(model_file_name);
```
The C API details are [here](../api/c-api.md).

### Performance Tuning
For performance tuning, please see guidance on this page: [ONNX Runtime Perf Tuning](../../how-to/tune-performance.md)

When/if using [onnxruntime_perf_test](https://github.com/microsoft/onnxruntime/tree/master/onnxruntime/test/perftest), use the flag -e armnn
