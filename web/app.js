(() => {
  const fileInput = document.getElementById('puzzle');
  const drop = document.getElementById('drop');
  const solveButton = document.getElementById('solve');
  const download = document.getElementById('download');
  const status = document.getElementById('status');
  const metrics = document.getElementById('metrics');
  const viewer = document.getElementById('viewer');
  const sceneSvg = document.getElementById('scene');
  const tapes = document.getElementById('tapes');
  const scrub = document.getElementById('scrub');
  const playButton = document.getElementById('play');
  const clock = document.getElementById('clock');
  const editName = document.getElementById('edit-name');
  const editMultiplier = document.getElementById('edit-multiplier');
  const editInput = document.getElementById('edit-input');
  const editOutput = document.getElementById('edit-output');
  const buildButton = document.getElementById('build');
  const downloadPuzzle = document.getElementById('download-puzzle');
  const editorStatus = document.getElementById('editor-status');
  const metricNodes = {
    cycles: document.getElementById('m-cycles'),
    cost: document.getElementById('m-cost'),
    area: document.getElementById('m-area'),
    instructions: document.getElementById('m-instructions'),
  };

  let modulePromise;
  let selectedFile = null;
  let puzzleBytes = null;
  let scene = null;
  let maxCycle = 0;
  let playTimer = null;

  const SVG_NS = 'http://www.w3.org/2000/svg';
  const UNIT = 24;
  const escapeHtml = (text) => String(text).replace(/[&<>"']/g, (c) => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
  }[c]));
  const DIRECTIONS = [[1, 0], [0, 1], [-1, 1], [-1, 0], [0, -1], [1, -1]];

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
    viewer.hidden = true;
    scene = null;
    stopPlayback();
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



  const SYMBOLS = {
    S: 1, A: 2, E: 3, F: 4, W: 5, Q: 6, G: 7, V: 8, C: 9,
    I: 10, T: 11, L: 12, '+': 13, '-': 14, R: 15, X: 16,
  };

  const parseAtoms = (text) => {
    const atoms = [];
    for (const line of text.split(/\n/)) {
      const trimmed = line.trim();
      if (!trimmed) continue;
      const [symbol, q, r] = trimmed.split(/\s+/);
      if (!(symbol in SYMBOLS) || q === undefined || r === undefined)
        throw new Error(`atom line must be "symbol q r": ${trimmed}`);
      const iq = Number(q), ir = Number(r);
      if (!Number.isInteger(iq) || !Number.isInteger(ir)) throw new Error(`bad coordinate: ${trimmed}`);
      atoms.push(SYMBOLS[symbol], iq, ir);
    }
    if (!atoms.length) throw new Error('at least one atom is required');
    return atoms;
  };

  const setEditorStatus = (text, kind = '') => {
    editorStatus.textContent = text;
    editorStatus.className = `status ${kind}`;
  };

  const buildPuzzle = async () => {
    buildButton.disabled = true;
    downloadPuzzle.classList.add('disabled');
    try {
      const input = parseAtoms(editInput.value);
      const output = parseAtoms(editOutput.value);
      const multiplier = Number(editMultiplier.value);
      if (!Number.isInteger(multiplier) || multiplier < 1) throw new Error('multiplier must be positive');
      const module = await ensureModule();
      const namePtr = module._malloc(new TextEncoder().encode(editName.value || 'Browser puzzle').length + 1);
      const name = editName.value || 'Browser puzzle';
      const encoded = new TextEncoder().encode(name);
      module.HEAPU8.set(encoded, namePtr);
      module.HEAPU8[namePtr + encoded.length] = 0;
      const inPtr = module._malloc(input.length * 4);
      const outPtr = module._malloc(output.length * 4);
      const resultPtrPtr = module._malloc(4);
      const lengthPtr = module._malloc(4);
      const errorPtrPtr = module._malloc(4);
      try {
        module.HEAP32.set(Int32Array.from(input), inPtr >>> 2);
        module.HEAP32.set(Int32Array.from(output), outPtr >>> 2);
        const ok = module.ccall('opusoptima_make_puzzle', 'number',
          ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number'],
          [namePtr, input.length / 3, inPtr, output.length / 3, outPtr, multiplier,
           resultPtrPtr, lengthPtr, errorPtrPtr]);
        const errorPtr = module.HEAPU32[errorPtrPtr >>> 2];
        if (!ok) throw new Error(errorPtr ? module.UTF8ToString(errorPtr) : 'puzzle build failed');
        if (errorPtr) module.ccall('opusoptima_free', null, ['number'], [errorPtr]);
        const resultPtr = module.HEAPU32[resultPtrPtr >>> 2];
        const length = module.HEAPU32[lengthPtr >>> 2];
        puzzleBytes = module.HEAPU8.slice(resultPtr, resultPtr + length);
        module.ccall('opusoptima_free', null, ['number'], [resultPtr]);
        selectedFile = { name: `${name}.puzzle` };
        solveButton.disabled = false;
        metrics.hidden = true;
        viewer.hidden = true;
        scene = null;
        stopPlayback();
        const blob = new Blob([puzzleBytes], { type: 'application/octet-stream' });
        downloadPuzzle.href = URL.createObjectURL(blob);
        downloadPuzzle.download = `${name}.puzzle`;
        downloadPuzzle.classList.remove('disabled');
        setEditorStatus(`Built "${name}" (${puzzleBytes.length} bytes). Press Solve.`, 'ok');
        setStatus(`Loaded generated puzzle "${name}".`);
      } finally {
        module._free(namePtr); module._free(inPtr); module._free(outPtr);
        module._free(resultPtrPtr); module._free(lengthPtr); module._free(errorPtrPtr);
      }
    } catch (error) {
      setEditorStatus(error?.message || String(error), 'bad');
    } finally {
      buildButton.disabled = false;
    }
  };

  const callDescribe = (module, solution) => {
    const puzzlePtr = module._malloc(puzzleBytes.length);
    const solutionPtr = module._malloc(solution.length);
    try {
      module.HEAPU8.set(puzzleBytes, puzzlePtr);
      module.HEAPU8.set(solution, solutionPtr);
      const jsonPtr = module.ccall(
        'opusoptima_describe', 'number',
        ['number', 'number', 'number', 'number'],
        [puzzlePtr, puzzleBytes.length, solutionPtr, solution.length]);
      if (!jsonPtr) throw new Error('The solution description was empty.');
      const json = module.UTF8ToString(jsonPtr);
      module.ccall('opusoptima_free', null, ['number'], [jsonPtr]);
      const parsed = JSON.parse(json);
      if (parsed.error) throw new Error(parsed.error);
      return parsed;
    } finally {
      module._free(puzzlePtr);
      module._free(solutionPtr);
    }
  };

  const pixel = (q, r) => ({
    x: (q + r / 2) * Math.sqrt(3) * UNIT,
    y: r * 1.5 * UNIT,
  });

  const hexPoints = (x, y, radius) => {
    const points = [];
    for (let i = 0; i < 6; ++i) {
      const angle = Math.PI / 180 * (60 * i - 30);
      points.push(`${(x + radius * Math.cos(angle)).toFixed(2)},${(y + radius * Math.sin(angle)).toFixed(2)}`);
    }
    return points.join(' ');
  };

  const objectLabel = (type) => {
    if (type.startsWith('glyph-')) return type.slice(6, 7).toUpperCase();
    if (type === 'out-std') return 'OUT';
    if (type === 'input') return 'IN';
    if (type === 'bonder') return 'B';
    if (type === 'unbonder') return 'U';
    if (type === 'bonder-prisma') return 'P';
    return type.replace('arm', 'A').slice(0, 4);
  };

  const objectColor = (type) => {
    if (type === 'input') return '#75d69c';
    if (type === 'out-std') return '#8cb8ff';
    if (type.startsWith('glyph-')) return '#f2b84b';
    if (type === 'bonder' || type === 'bonder-prisma') return '#d58cff';
    if (type === 'unbonder') return '#ff9f7a';
    if (type === 'track') return '#586579';
    return '#c4cedd';
  };

  const trackCells = (object) => {
    const cells = [];
    const segments = object.body?.[4] ?? 0;
    let q = object.q;
    let r = object.r;
    cells.push([q, r]);
    for (let i = 0; i < segments; ++i) {
      const direction = ((object.body[5 + i * 2] ?? 0) % 6 + 6) % 6;
      const length = object.body[6 + i * 2] ?? 1;
      const [dq, dr] = DIRECTIONS[direction];
      for (let step = 0; step < length; ++step) {
        q += dq;
        r += dr;
        cells.push([q, r]);
      }
    }
    return cells;
  };

  const activeInstruction = (object, time) => {
    let active = null;
    for (const [action, at] of object.tape || []) {
      if (at <= time) active = { action, at };
      else break;
    }
    return active;
  };

  const activeIndex = (tape, time) => {
    let index = -1;
    for (let i = 0; i < tape.length; ++i) {
      if (tape[i][1] <= time) index = i;
      else break;
    }
    return index;
  };

  const drawScene = (time) => {
    if (!scene) return;
    const points = [];
    const collect = (q, r) => points.push(pixel(q, r));
    for (const molecule of [...scene.puzzle.inputs, ...scene.puzzle.outputs]) {
      for (const atom of molecule.atoms) collect(atom.q, atom.r);
    }
    for (const object of scene.objects) {
      if (object.type === 'track') {
        for (const [q, r] of trackCells(object)) collect(q, r);
      } else {
        collect(object.q, object.r);
      }
    }
    if (!points.length) return;
    const minX = Math.min(...points.map((p) => p.x)) - UNIT * 2;
    const maxX = Math.max(...points.map((p) => p.x)) + UNIT * 2;
    const minY = Math.min(...points.map((p) => p.y)) - UNIT * 2;
    const maxY = Math.max(...points.map((p) => p.y)) + UNIT * 2;
    const width = maxX - minX;
    const height = maxY - minY;
    sceneSvg.setAttribute('viewBox', `${minX} ${minY} ${width} ${height}`);
    const parts = [];
    parts.push(`<rect x="${minX}" y="${minY}" width="${width}" height="${height}" fill="#0d1117"/>`);
    const bondLine = (a, b, color = '#526078') => {
      const pa = pixel(a[0], a[1]), pb = pixel(b[0], b[1]);
      parts.push(`<line x1="${pa.x}" y1="${pa.y}" x2="${pb.x}" y2="${pb.y}" stroke="${color}" stroke-width="3"/>`);
    };
    for (const molecule of [...scene.puzzle.inputs, ...scene.puzzle.outputs]) {
      for (const bond of molecule.bonds) bondLine(bond.a, bond.b);
    }
    for (const object of scene.objects) {
      if (object.type !== 'track') continue;
      const cells = trackCells(object);
      for (let i = 1; i < cells.length; ++i) bondLine(cells[i - 1], cells[i], '#586579');
    }
    const atomColor = (symbol) => ({
      S: '#e8e8e8', A: '#b8d8ff', E: '#c8a46b', F: '#ff8f6b', W: '#6bb8ff',
      Q: '#b9b9d8', G: '#f2c94c', V: '#c9d0da', C: '#d58c6b', I: '#9aa8b8',
      T: '#9aa8b8', L: '#828b9a', [String.fromCharCode(0xb7)]: '#e0a0ff',
    }[symbol] || '#d5dae3');
    const atom = (q, r, symbol, outline = '#111827') => {
      const p = pixel(q, r);
      parts.push(`<circle cx="${p.x}" cy="${p.y}" r="${UNIT * 0.52}" fill="${atomColor(symbol)}" stroke="${outline}" stroke-width="1.5"/>`);
      if (symbol) parts.push(`<text x="${p.x}" y="${p.y + 4}" text-anchor="middle" font-size="11" font-family="monospace" fill="#111827">${symbol}</text>`);
    };
    for (const molecule of scene.puzzle.inputs) for (const a of molecule.atoms) atom(a.q, a.r, a.symbol, '#75d69c');
    for (const molecule of scene.puzzle.outputs) for (const a of molecule.atoms) atom(a.q, a.r, a.symbol, '#8cb8ff');
    for (const object of scene.objects) {
      if (object.type === 'track') continue;
      const p = pixel(object.q, object.r);
      const color = objectColor(object.type);
      if (object.type.startsWith('glyph-')) {
        parts.push(`<polygon points="${hexPoints(p.x, p.y, UNIT * 0.82)}" fill="#2b2415" stroke="${color}" stroke-width="2"/>`);
        parts.push(`<text x="${p.x}" y="${p.y + 5}" text-anchor="middle" font-size="13" font-weight="700" font-family="monospace" fill="${color}">${escapeHtml(objectLabel(object.type))}</text>`);
      } else if (object.type === 'bonder' || object.type === 'bonder-prisma' || object.type === 'unbonder') {
        parts.push(`<rect x="${p.x - UNIT * 0.55}" y="${p.y - UNIT * 0.55}" width="${UNIT * 1.1}" height="${UNIT * 1.1}" rx="4" fill="#171d28" stroke="${color}" stroke-width="2"/>`);
        parts.push(`<text x="${p.x}" y="${p.y + 4}" text-anchor="middle" font-size="11" font-family="monospace" fill="${color}">${escapeHtml(objectLabel(object.type))}</text>`);
      } else {
        parts.push(`<circle cx="${p.x}" cy="${p.y}" r="${UNIT * 0.68}" fill="#171d28" stroke="${color}" stroke-width="2"/>`);
        parts.push(`<text x="${p.x}" y="${p.y + 4}" text-anchor="middle" font-size="9" font-family="monospace" fill="${color}">${escapeHtml(objectLabel(object.type))}</text>`);
      }
      const active = activeInstruction(object, time);
      if (active) parts.push(`<text x="${p.x}" y="${p.y - UNIT * 0.95}" text-anchor="middle" font-size="12" font-weight="700" font-family="monospace" fill="#f2b84b">${escapeHtml(active.action)}</text>`);
    }
    sceneSvg.innerHTML = parts.join('');
  };

  const drawTapes = (time) => {
    if (!scene) return;
    const rows = [];
    for (const object of scene.objects) {
      if (!object.tape?.length) continue;
      const name = escapeHtml(`${object.type} @ ${object.q},${object.r}`);
      let code = '';
      const current = activeIndex(object.tape, time);
      object.tape.forEach(([action], index) => {
        const safe = escapeHtml(action);
        code += index === current ? `<b>${safe}</b>` : safe;
      });
      rows.push(`<div class="tape-row"><span class="tape-name">${name}</span><span class="tape-code">${code}</span></div>`);
    }
    tapes.innerHTML = rows.join('');
  };

  const renderAt = (time) => {
    const t = Math.max(0, Math.min(maxCycle, Number(time) || 0));
    scrub.value = String(t);
    clock.textContent = String(t);
    drawScene(t);
    drawTapes(t);
  };

  const stopPlayback = () => {
    if (playTimer) clearInterval(playTimer);
    playTimer = null;
    playButton.textContent = 'Play';
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
      scene = callDescribe(module, solution);
      maxCycle = Math.max(0, ...scene.objects.flatMap((object) => (object.tape || []).map(([, at]) => at)));
      viewer.hidden = false;
      scrub.max = String(maxCycle);
      stopPlayback();
      renderAt(0);
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

  scrub.addEventListener('input', () => renderAt(scrub.value));
  playButton.addEventListener('click', () => {
    if (!scene) return;
    if (playTimer) { stopPlayback(); return; }
    playButton.textContent = 'Pause';
    let t = Number(scrub.value);
    playTimer = setInterval(() => {
      t += 1;
      if (t > maxCycle) t = 0;
      renderAt(t);
    }, 110);
  });

  buildButton.addEventListener('click', () => { buildPuzzle(); });

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
