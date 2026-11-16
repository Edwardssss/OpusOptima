(() => {
  const fileInput = document.getElementById('puzzle');
  const drop = document.getElementById('drop');
  const solveButton = document.getElementById('solve');
  const download = document.getElementById('download');
  const status = document.getElementById('status');
  const metrics = document.getElementById('metrics');
  const metricNodes = {
    cycles: document.getElementById('m-cycles'),
    cost: document.getElementById('m-cost'),
    area: document.getElementById('m-area'),
    instructions: document.getElementById('m-instructions'),
  };

  let modulePromise;
  let selectedFile = null;
  let puzzleBytes = null;

  const setStatus = (text, kind = '') => {
    status.textContent = text;
    status.className = `status ${kind}`;
  };

  const ensureModule = () => {
    if (!modulePromise) modulePromise = OpusOptimaModule();
    return modulePromise;
  };

  const readFile = async (file) => {
    selectedFile = file;
    puzzleBytes = new Uint8Array(await file.arrayBuffer());
    solveButton.disabled = false;
    download.classList.add('disabled');
    download.removeAttribute('href');
    metrics.hidden = true;
    setStatus(`${file.name} loaded (${puzzleBytes.length} bytes).`);
  };

  const callSolver = (module) => {
    const puzzlePtr = module._malloc(puzzleBytes.length);
    const solutionPtrPtr = module._malloc(4);
    const lengthPtr = module._malloc(4);
    const errorPtrPtr = module._malloc(4);
    try {
      module.HEAPU8.set(puzzleBytes, puzzlePtr);
      const ok = module.ccall(
        'opusoptima_solve', 'number',
        ['number', 'number', 'number', 'number', 'number'],
        [puzzlePtr, puzzleBytes.length, solutionPtrPtr, lengthPtr, errorPtrPtr]);
      const errorPtr = module.HEAPU32[errorPtrPtr >>> 2];
      const error = errorPtr ? module.UTF8ToString(errorPtr) : '';
      if (errorPtr) module.ccall('opusoptima_free', null, ['number'], [errorPtr]);
      if (!ok) throw new Error(error || 'The solver produced no solution.');
      const solutionPtr = module.HEAPU32[solutionPtrPtr >>> 2];
      const length = module.HEAPU32[lengthPtr >>> 2];
      const bytes = module.HEAPU8.slice(solutionPtr, solutionPtr + length);
      module.ccall('opusoptima_free', null, ['number'], [solutionPtr]);
      return bytes;
    } finally {
      module._free(puzzlePtr);
      module._free(solutionPtrPtr);
      module._free(lengthPtr);
      module._free(errorPtrPtr);
    }
  };

  const callVerifier = (module, solution) => {
    const puzzlePtr = module._malloc(puzzleBytes.length);
    const solutionPtr = module._malloc(solution.length);
    try {
      module.HEAPU8.set(puzzleBytes, puzzlePtr);
      module.HEAPU8.set(solution, solutionPtr);
      const jsonPtr = module.ccall(
        'opusoptima_verify', 'number',
        ['number', 'number', 'number', 'number'],
        [puzzlePtr, puzzleBytes.length, solutionPtr, solution.length]);
      if (!jsonPtr) throw new Error('The verifier returned no result.');
      const json = module.UTF8ToString(jsonPtr);
      module.ccall('opusoptima_free', null, ['number'], [jsonPtr]);
      return JSON.parse(json);
    } finally {
      module._free(puzzlePtr);
      module._free(solutionPtr);
    }
  };

  solveButton.addEventListener('click', async () => {
    if (!puzzleBytes) return;
    solveButton.disabled = true;
    download.classList.add('disabled');
    download.removeAttribute('href');
    metrics.hidden = true;
    setStatus('Loading WebAssembly module…');
    try {
      const module = await ensureModule();
      setStatus('Solving with the upstream Standard port…');
      await new Promise((resolve) => setTimeout(resolve, 20));
      const started = performance.now();
      const solution = callSolver(module);
      const elapsed = Math.round(performance.now() - started);
      setStatus('Verifying with the in-process omsim check…');
      const report = callVerifier(module, solution);
      if (report.status !== 'passed') {
        throw new Error(`omsim rejected the candidate: ${report.detail || report.status}`);
      }
      for (const key of Object.keys(metricNodes)) metricNodes[key].textContent = report[key] ?? '—';
      metrics.hidden = false;
      const base = (selectedFile?.name || 'puzzle').replace(/\.puzzle$/i, '');
      const blob = new Blob([solution], { type: 'application/octet-stream' });
      download.href = URL.createObjectURL(blob);
      download.download = `${base}.solution`;
      download.classList.remove('disabled');
      setStatus(`Solved and verified in ${elapsed} ms.`, 'ok');
    } catch (error) {
      setStatus(error?.message || String(error), 'bad');
    } finally {
      solveButton.disabled = false;
    }
  });

  fileInput.addEventListener('change', () => {
    if (fileInput.files?.[0]) readFile(fileInput.files[0]).catch((error) => setStatus(String(error), 'bad'));
  });
  for (const event of ['dragenter', 'dragover']) {
    drop.addEventListener(event, (ev) => { ev.preventDefault(); drop.classList.add('over'); });
  }
  for (const event of ['dragleave', 'drop']) {
    drop.addEventListener(event, (ev) => { ev.preventDefault(); drop.classList.remove('over'); });
  }
  drop.addEventListener('drop', (ev) => {
    const file = ev.dataTransfer?.files?.[0];
    if (file) readFile(file).catch((error) => setStatus(String(error), 'bad'));
  });
})();
