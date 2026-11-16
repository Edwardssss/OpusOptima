// Build a tiny salt->salt puzzle through the editor ABI, solve it and verify it.
// Usage: node web/editor-smoke.cjs <module.js>.
const path = require('path');

(async () => {
  const modulePath = process.argv[2];
  if (!modulePath) throw new Error('usage: editor-smoke.cjs <module.js>');
  const module = await require(path.resolve(modulePath))();
  const name = 'editor smoke';
  const encodedName = Buffer.from(name + '\0', 'utf8');
  const namePtr = module._malloc(encodedName.length);
  const inputPtr = module._malloc(12);
  const outputPtr = module._malloc(12);
  const puzzlePtrPtr = module._malloc(4);
  const lengthPtr = module._malloc(4);
  const errorPtrPtr = module._malloc(4);
  try {
    module.HEAPU8.set(encodedName, namePtr);
    module.HEAP32.set(new Int32Array([1, 0, 0]), inputPtr >>> 2);
    module.HEAP32.set(new Int32Array([1, 0, 0]), outputPtr >>> 2);
    const ok = module.ccall('opusoptima_make_puzzle', 'number',
      ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number'],
      [namePtr, 1, inputPtr, 1, outputPtr, 1, puzzlePtrPtr, lengthPtr, errorPtrPtr]);
    const errorPtr = module.HEAPU32[errorPtrPtr >>> 2];
    if (!ok) throw new Error(errorPtr ? module.UTF8ToString(errorPtr) : 'editor build failed');
    if (errorPtr) module.ccall('opusoptima_free', null, ['number'], [errorPtr]);
    const puzzlePtr = module.HEAPU32[puzzlePtrPtr >>> 2];
    const puzzleLength = module.HEAPU32[lengthPtr >>> 2];
    const solutionPtrPtr = module._malloc(4);
    const solutionLengthPtr = module._malloc(4);
    const solveErrorPtrPtr = module._malloc(4);
    try {
      const solved = module.ccall('opusoptima_solve', 'number',
        ['number', 'number', 'number', 'number', 'number'],
        [puzzlePtr, puzzleLength, solutionPtrPtr, solutionLengthPtr, solveErrorPtrPtr]);
      const solveErrorPtr = module.HEAPU32[solveErrorPtrPtr >>> 2];
      if (!solved) throw new Error(solveErrorPtr ? module.UTF8ToString(solveErrorPtr) : 'editor puzzle did not solve');
      const solutionPtr = module.HEAPU32[solutionPtrPtr >>> 2];
      const solutionLength = module.HEAPU32[solutionLengthPtr >>> 2];
      const solutionCopy = module._malloc(solutionLength);
      module.HEAPU8.set(module.HEAPU8.subarray(solutionPtr, solutionPtr + solutionLength), solutionCopy);
      const verifyPtr = module.ccall('opusoptima_verify', 'number',
        ['number', 'number', 'number', 'number'],
        [puzzlePtr, puzzleLength, solutionCopy, solutionLength]);
      if (!verifyPtr) throw new Error('editor verifier returned no result');
      const report = JSON.parse(module.UTF8ToString(verifyPtr));
      module.ccall('opusoptima_free', null, ['number'], [verifyPtr]);
      module._free(solutionCopy);
      if (report.status !== 'passed') throw new Error(`editor verification failed: ${JSON.stringify(report)}`);
      console.log(`editor: ${puzzleLength} puzzle bytes, ${solutionLength} solution bytes, ${JSON.stringify(report)}`);
    } finally {
      module._free(solutionPtrPtr); module._free(solutionLengthPtr); module._free(solveErrorPtrPtr);
    }
  } finally {
    module._free(namePtr); module._free(inputPtr); module._free(outputPtr);
    module._free(puzzlePtrPtr); module._free(lengthPtr); module._free(errorPtrPtr);
  }
})().catch((error) => { console.error(error); process.exit(1); });
