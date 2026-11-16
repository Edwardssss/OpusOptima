// Load the generated Emscripten module in Node, solve one puzzle and require the
// in-process omsim check to pass. Usage: node web/smoke-test.cjs <module.js> <puzzle>.
const fs = require('fs');

(async () => {
  const [modulePath, puzzlePath] = process.argv.slice(2);
  if (!modulePath || !puzzlePath) throw new Error('usage: smoke-test.cjs <module.js> <puzzle>');
  const factory = require(require('path').resolve(modulePath));
  const module = await factory();
  const puzzle = fs.readFileSync(puzzlePath);
  const puzzlePtr = module._malloc(puzzle.length);
  const solutionPtrPtr = module._malloc(4);
  const lengthPtr = module._malloc(4);
  const errorPtrPtr = module._malloc(4);
  try {
    module.HEAPU8.set(puzzle, puzzlePtr);
    const ok = module.ccall('opusoptima_solve', 'number',
      ['number', 'number', 'number', 'number', 'number'],
      [puzzlePtr, puzzle.length, solutionPtrPtr, lengthPtr, errorPtrPtr]);
    const errorPtr = module.HEAPU32[errorPtrPtr >>> 2];
    if (!ok) throw new Error(errorPtr ? module.UTF8ToString(errorPtr) : 'solver returned no candidate');
    if (errorPtr) module.ccall('opusoptima_free', null, ['number'], [errorPtr]);
    const solutionPtr = module.HEAPU32[solutionPtrPtr >>> 2];
    const length = module.HEAPU32[lengthPtr >>> 2];
    const solution = module.HEAPU8.slice(solutionPtr, solutionPtr + length);
    module.ccall('opusoptima_free', null, ['number'], [solutionPtr]);
    const solutionCopy = module._malloc(solution.length);
    module.HEAPU8.set(solution, solutionCopy);
    const jsonPtr = module.ccall('opusoptima_verify', 'number',
      ['number', 'number', 'number', 'number'],
      [puzzlePtr, puzzle.length, solutionCopy, solution.length]);
    if (!jsonPtr) throw new Error('verifier returned no result');
    const report = JSON.parse(module.UTF8ToString(jsonPtr));
    module.ccall('opusoptima_free', null, ['number'], [jsonPtr]);
    const describePtr = module.ccall('opusoptima_describe', 'number',
      ['number', 'number', 'number', 'number'],
      [puzzlePtr, puzzle.length, solutionCopy, solution.length]);
    if (!describePtr) throw new Error('solution description returned no result');
    const description = JSON.parse(module.UTF8ToString(describePtr));
    module.ccall('opusoptima_free', null, ['number'], [describePtr]);
    module._free(solutionCopy);
    if (report.status !== 'passed') throw new Error(`verification failed: ${JSON.stringify(report)}`);
    if (description.error || !description.objects?.length)
      throw new Error(`description failed: ${JSON.stringify(description)}`);
    console.log(`${puzzlePath}: ${length} bytes, ${JSON.stringify(report)}, ${description.objects.length} objects`);
  } finally {
    module._free(puzzlePtr);
    module._free(solutionPtrPtr);
    module._free(lengthPtr);
    module._free(errorPtrPtr);
  }
})().catch((error) => { console.error(error); process.exit(1); });
