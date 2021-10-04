// Copyright (c) Microsoft Corporation. All rights reserved.
// This file is adapted from microsoft/DeepSpeed
// compat.h

/* Copyright 2020 The Microsoft DeepSpeed Team
   Copyright NVIDIA/apex
   This file is adapted from fused adam in NVIDIA/apex, commit a109f85
*/

#ifndef TORCH_CHECK
#define TORCH_CHECK AT_CHECK
#endif

#ifdef VERSION_GE_1_3
#define DATA_PTR data_ptr
#else
#define DATA_PTR data
#endif
