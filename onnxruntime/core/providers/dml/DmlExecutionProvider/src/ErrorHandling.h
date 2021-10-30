// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#pragma once
#ifdef ORT_NO_EXCEPTIONS
#define ORT_CATCH_RETURN 
#else
#define ORT_CATCH_RETURN CATCH_RETURN()
#endif

#ifdef ORT_NO_EXCEPTIONS
#define ORT_CATCH_GENERIC ORT_CATCH(...) 
#else
#define ORT_CATCH_GENERIC catch(...)
#endif

#ifdef ORT_NO_EXCEPTIONS
#define THROW_IF_NOT_OK(status)   \
    do {                          \
        auto _status = status;    \
        if (!_status.IsOK()) {    \
            ORT_THROW(status);    \
        }                         \
    } while (0);
#else
#define THROW_IF_NOT_OK(status)                                                                                 \
    do {                                                                                                        \
        auto _status = status;                                                                                  \
        if (!_status.IsOK())                                                                                    \
        {                                                                                                       \
             THROW_HR(StatusCodeToHRESULT(static_cast<onnxruntime::common::StatusCode>(_status.Code())));       \
        }                                                                                                       \
    } while (0)
#endif

#ifdef ORT_NO_EXCEPTIONS
#define ORT_THROW_IF_FAILED(hr) \
    if(!SUCCEEDED(hr))          \
    {                           \
        ORT_THROW(hr);          \
    }
#else
#define ORT_THROW_IF_FAILED(hr) THROW_IF_FAILED(hr)
#endif

#ifdef ORT_NO_EXCEPTIONS
#define ORT_THROW_HR(hr) ORT_THROW(hr)
#else
#define ORT_THROW_HR(hr) THROW_HR(hr)
#endif

#ifdef ORT_NO_EXCEPTIONS
#define ORT_THROW_HR_IF(hr, condition) ORT_ENFORCE(condition, hr)                      
#else
#define ORT_THROW_HR_IF(hr, condition) THROW_HR_IF(hr, condition) 
#endif
