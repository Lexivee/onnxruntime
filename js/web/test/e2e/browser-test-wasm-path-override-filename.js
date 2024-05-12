// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

'use strict';

it('Browser E2E testing - WebAssembly backend (path override filename)', async function() {
  // check base URL from karma args
  if (typeof __ort_arg_base === 'undefined') {
    throw new Error('karma flag --base-url=<BASE_URL> is required');
  }

  // disable SIMD and multi-thread
  ort.env.wasm.numThreads = 1;
  ort.env.wasm.simd = false;

  // override .wasm file path for 'ort-wasm.wasm'
  const overrideWasmUrl = new URL('./test-wasm-path-override/renamed.wasm', __ort_arg_base).href;
  console.log(`ort.env.wasm.wasmPaths['wasm'] = ${JSON.stringify(overrideWasmUrl)};`);

  const overrideMjsUrl = new URL('./test-wasm-path-override/renamed.mjs', __ort_arg_base).href;
  console.log(`ort.env.wasm.wasmPaths['mjs'] = ${JSON.stringify(overrideMjsUrl)};`);

  ort.env.wasm.wasmPaths = {'wasm': overrideWasmUrl, 'mjs': overrideMjsUrl};

  await testFunction(ort, {executionProviders: ['wasm']});
});
