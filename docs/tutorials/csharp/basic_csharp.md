---
title:  Basic C# Tutorial
description: Basic usage of C# API
parent: Inference with C#
grand_parent: Tutorials
has_children: false
nav_order: 1
---


# C# Tutorial: Basic

Here is a simple tutorial for getting started with running inference on an existing ONNX model for a given input data.
The model is typically trained using any of the well-known training frameworks and then exported into the ONNX format. 

Note, that the following classes `NamedOnnxValue`, `DisposableNamedOnnxValue`, `FixedBufferOnnxValue` are going
to be deprecated in the future. They are not recommended for new code.

The new `OrtValue` based API is the recommended approach. The `OrtValue` API generates less garbage and is more performant.
Some scenarios indicated 4x performance improvement over the previous API and significantly less garbage.
It provides uniform access to data via `ReadOnlySpan<T>` and `Span<T>` structures, regardless of its location, managed or unmanaged.

`DenseTensor` class can still be used for multi-dimensional access to the data since the new `Span` based API feature
only a 1-D index. However, some reported a slow performance when using `DenseTensor` class multi-dimensional access.
One can then create an OrtValue on top of the tensors data. 

`ShapeUtils` class provides some help to deal with multi-dimensional indices for OrtValues.

`OrtValue` based API provides direct native memory access in a type safe manner using `ReadOnlySpan<T>` and `Span<T>` stack bases structures.
OrtValue is a universal container that can hold different ONNX types, such as tensors, maps, and sequences.
It always existed in the onnxruntime library, but was not exposed in the C# API.

As before, `OrtValues` can be created directly on top of the managed `unmanaged` (struct based blittable types)  arrays.
Read MS documentation on `blittable` data types. onnxruntime C# API allows use of managed buffers for input or output.

If output shapes are known, one can pre-allocate `OrtValue` on top of the managed or unmanaged allocations and supply
those OrtValues to be used as outputs. Due to this fact, the need for `IOBinding` is greatly diminished.

String data is represented as UTF-16 string objects in C#. It will still need to be copied and converted to UTF-8 to the native
memory. However, that conversion is now more optimized and is done in a single pass without intermediate byte arrays.
The same applies to string `OrtValue` tensors returned as outputs. Character based API now operates on `Span<char>`,
`ReadOnlySpan<char>`, and `ReadOnlyMemory<char>` objects. This adds flexibility to the API and allows to avoid unnecessary copies.

Except some of the above deprecated API classes, nearly all of C# API classes are `IDisposable`.
Meaning they need to be disposed after use, otherwise you will get memory leaks.
Because OrtValues are used to hold tensor data, the sizes of the leaks can be huge. They are likely
to accumulate with each `Run` call, as each inference call requires input OrtValues and returns output OrtValues.
Do not hold your breath for finalizers which are not guaranteed to ever run, and if they do, they do it
when it is too late.

This includes `SessionOptions`, `RunOptions`, `InferenceSession`, `OrtValue`. Run() calls return `IDisposableCollection`
that allows to dispose all of the containing objects in one statement or `using`. This is because these objects
own some native resource, often a native object.

Not disposing `OrtValue` that was created on top of the managed buffer would result in
that buffer pinned in memory indefinitely. Such a buffer can not be garbage collected or moved in memory.

`OrtValue`s that were created on top of the native onnxruntime memory should also be disposed of promptly.
Otherwise, the native memory will not be deallocated. OrtValues returned by `Run()` usually hold native memory.

GC can not operate on native memory or any other native resources.

The `using` statement or a block is a convenient way to ensure that the objects are disposed.
`InferenceSession` can be a long lived object and a member of another class. It eventually must also need to be disposed.
This means, the containing class also would have to be made disposable to achieve this.

OrtValue API also provides visitor like API to walk ONNX maps and sequences.
This is a more efficient way to access Onnxruntime data.

To start scoring using the model, open a session using the `InferenceSession` class, passing in the file path to the model as a parameter.

```cs
using var session = new InferenceSession("model.onnx");
```

Once a session is created, you can execute queries using the `Run` method of the  `InferenceSession` object.

```cs

float[] sourceData;  // assume your data is loaded into a flat float array
long[] dimensions;    // and the dimensions of the input is stored here

// Create a OrtValue on top of the sourceData array
using var inputOrtValue = OrtValue.CreateTensorValueFromMemory(sourceData, dimensions);

var inputs = new Dictionary<string, OrtValue> {
    { "name1",  inputOrtValue }
};


using var runOptions = new RunOptions();

// Pass inputs and request the first output
// Note that the output is a disposable collection that holds OrtValues
using var output = session.Run(runOptions, inputs, session.OutputNames[0]);

var output_0 = output[0];

// Assuming the output contains a tensor of float data, you can access it as follows
// Returns Span<float> which points directly to native memory.
var outputData = output_0.GetTensorDataAsSpan<float>();

// If you are interested in more information about output, request its type and shape
// Assuming it is a tensor
// This is not disposable, will be GCed
// There you can request Shape, ElementDataType, etc
var tensorTypeAndShape = output_0.GetTensorTypeAndShape();

```
You can still use `Tensor` class for data manipulation if you have existing code that does it.
Then create `OrtValue` on top of Tensor buffer.

```cs

// Create and manipulate the data using tensor interface
DenseTensor<float> t1 = new DenseTensor<float>(sourceData, dimensions);

// One minor inconvenience is that Tensor class operates on `int` dimensions and indices.
// OrtValue dimensions are `long`. This is required, because `OrtValue` talks directly to
// Ort API and the library uses long dimensions.

// Convert dims to long[]
var shape = Array.Convert<int,long>(dimensions, Convert.ToInt64);

using var inputOrtValue = OrtValue.CreateTensorValueFromMemory(OrtMemoryInfo.DefaultInstance,
    t1.Buffer, shape);

```

Here is a way to populate a string tensor. Strings can not be mapped, and must be copy/converted to native memory.
To that end we pre-allocate a native tensor of empty strings with specified dimensions, and then
set individual strings by index.


```cs

string[] strs = { "Hello", "Ort", "World" };
long[] shape = { 1, 1, 3 };
var elementsNum = ShapeUtils.GetSizeForShape(shape);

using var strTensor = OrtValue.CreateTensorWithEmptyStrings(OrtAllocator.DefaultInstance, shape);

for (long i = 0; i < elementsNum; ++i)
{
    strTensor.StringTensorSetElementAt(strs[i].AsSpan(), i);
}

```


