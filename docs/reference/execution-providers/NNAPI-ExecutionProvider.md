---
title: NNAPI
parent: Execution Providers
grand_parent: Reference
nav_order: 7
---


# NNAPI Execution Provider
{: .no_toc }

[Android Neural Networks API (NNAPI)](https://developer.android.com/ndk/guides/neuralnetworks) is a unified interface to CPU, GPU, and NN accelerators on Android.

## Contents
{: .no_toc }

* TOC placeholder
{:toc}

## Minimum requirements

The NNAPI EP requires Android devices with Android 8.1 or higher, it is recommended to use Android devices with Android 9 or higher to achieve optimal performance.

## Build NNAPI EP

For build instructions, please see the [BUILD page](../../how-to/build.md#Android-NNAPI-Execution-Provider).

## Using NNAPI EP in C/C++

To use NNAPI EP for inferencing, please register it as below.
```
string log_id = "Foo";
auto logging_manager = std::make_unique<LoggingManager>
(std::unique_ptr<ISink>{new CLogSink{}},
                                  static_cast<Severity>(lm_info.default_warning_level),
                                  false,
                                  LoggingManager::InstanceType::Default,
                                  &log_id)
Environment::Create(std::move(logging_manager), env)
InferenceSession session_object{so,env};
session_object.RegisterExecutionProvider(std::make_unique<::onnxruntime::NnapiExecutionProvider>());
status = session_object.Load(model_file_name);
```
The C API details are [here](../api/c-api.md).
