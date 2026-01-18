#pragma once
#include "onnxruntime_c_api.h"

// The caller owns the domain and releases it after all consuming sessions.
OrtCustomOpDomain* createSsruDomain(const OrtApi* api);

// Preferred host backend; matrices with unsupported dimensions still use InvokeOp.
// Safe to call from baseline code before any AVXVNNI translation unit is entered.
const char* ssruBackend() noexcept;
