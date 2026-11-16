// Build a multi-port production puzzle through the advanced JSON editor ABI.
// Usage: node web/json-smoke.cjs <module.js>
const path = require('path');

(async () => {
  const modulePath = process.argv[2];
  if (!modulePath) throw new Error('usage: json-smoke.cjs <module.js>');
  const module = await require(path.resolve(modulePath))();
  const spec = {
    name: 'multi-port production smoke',
    outputMultiplier: 1,
    permissionBits: '0x17c7370f',
    inputs: [
      { atoms: [{ type: 1, q: 0, r: 0 }] },
      { atoms: [{ type: 2, q: 0, r: 0 }] },
    ],
    outputs: [
      { atoms: [{ type: 1, q: 0, r: 0 }] },
      { atoms: [{ type: 2, q: 0, r: 0 }] },
    ],
    production: {
      shrinkLeft: false,
      shrinkRight: false,
      isolation: false,
      chambers: [{ q: 0, r: 0, type: 'Large' }],
      conduits: [{ a: [0, 0], b: [1, 0], path: [[0, 0], [1, 0]] }],
      vials: [{ q: 2, r: 0, top: true, count: 1 }],
    },
  };
  const encoded = Buffer.from(JSON.stringify(spec) + '\0', 'utf8');
  const jsonPtr = module._malloc(encoded.length);
  const puzzlePtrPtr = module._malloc(4);
  const lengthPtr = module._malloc(4);
  const errorPtrPtr = module._malloc(4);
  try {
    module.HEAPU8.set(encoded, jsonPtr);
    const ok = module.ccall('opusoptima_make_puzzle_json', 'number',
      ['number', 'number', 'number', 'number'],
      [jsonPtr, puzzlePtrPtr, lengthPtr, errorPtrPtr]);
    const errorPtr = module.HEAPU32[errorPtrPtr >>> 2];
    if (!ok) throw new Error(errorPtr ? module.UTF8ToString(errorPtr) : 'JSON editor failed');
    if (errorPtr) module.ccall('opusoptima_free', null, ['number'], [errorPtr]);
    const puzzlePtr = module.HEAPU32[puzzlePtrPtr >>> 2];
    const length = module.HEAPU32[lengthPtr >>> 2];
    if (length < 80) throw new Error(`unexpectedly small production puzzle: ${length} bytes`);
    module.ccall('opusoptima_free', null, ['number'], [puzzlePtr]);
    console.log(`json editor: ${length} bytes, ${spec.inputs.length} inputs, ${spec.outputs.length} outputs, production metadata`);
  } finally {
    module._free(jsonPtr); module._free(puzzlePtrPtr); module._free(lengthPtr); module._free(errorPtrPtr);
  }
})().catch((error) => { console.error(error); process.exit(1); });
