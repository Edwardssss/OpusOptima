// Visual puzzle builder: arbitrary atoms/bonds, multiple ports and production
// metadata. It serialises the same JSON schema as the advanced editor and calls
// the existing WASM puzzle writer.
(() => {
  const root = document.getElementById('puzzle-builder');
  if (!root) return;

  const tr = (en, zh) => (globalThis.OpusI18n?.lang === 'zh' ? zh : en);
  const symbols = [
    [1, 'S', 'Salt'], [2, 'A', 'Air'], [3, 'E', 'Earth'], [4, 'F', 'Fire'],
    [5, 'W', 'Water'], [6, 'Q', 'Quicksilver'], [7, 'G', 'Gold'], [8, 'V', 'Silver'],
    [9, 'C', 'Copper'], [10, 'I', 'Iron'], [11, 'T', 'Tin'], [12, 'L', 'Lead'],
    [13, '+', 'Vitae'], [14, '-', 'Mors'], [15, 'R', 'Repeat'], [16, 'X', 'Quintessence'],
  ];
  const atomByType = new Map(symbols.map(([type, symbol]) => [type, symbol]));
  const UNIT = 24;

  const state = {
    name: 'Custom puzzle',
    multiplier: 1,
    permissions: '0x17c7370f',
    inputs: [{ atoms: [{ type: 1, q: 0, r: 0 }], bonds: [] }],
    outputs: [{ atoms: [{ type: 1, q: 0, r: 0 }], bonds: [] }],
    activeKind: 'inputs',
    activeIndex: 0,
    tool: 'atom',
    atomType: 1,
    bondBits: 1,
    selected: null,
    bondStart: null,
    production: null,
  };

  const activeMolecule = () => state[state.activeKind][state.activeIndex];
  const pixel = (q, r) => ({ x: (q + r / 2) * Math.sqrt(3) * UNIT, y: r * 1.5 * UNIT });
  const hexPoints = (x, y, radius) => {
    const out = [];
    for (let i = 0; i < 6; ++i) {
      const a = Math.PI / 180 * (60 * i - 30);
      out.push(`${(x + radius * Math.cos(a)).toFixed(2)},${(y + radius * Math.sin(a)).toFixed(2)}`);
    }
    return out.join(' ');
  };
  const atomAt = (q, r) => activeMolecule().atoms.find((atom) => atom.q === q && atom.r === r);
  const bondBetween = (a, b) => activeMolecule().bonds.find((bond) =>
    (bond.from.q === a.q && bond.from.r === a.r && bond.to.q === b.q && bond.to.r === b.r) ||
    (bond.from.q === b.q && bond.from.r === b.r && bond.to.q === a.q && bond.to.r === a.r));

  const setStatus = (text, kind = '') => {
    const status = document.getElementById('builder-status');
    status.textContent = text;
    status.className = `status ${kind}`;
  };

  const renderPorts = () => {
    const host = document.getElementById('builder-ports');
    const rows = [];
    for (const kind of ['inputs', 'outputs']) {
      const items = state[kind];
      rows.push(`<div class="port-group"><div class="port-head"><strong>${kind === 'inputs' ? tr('Inputs', '输入') : tr('Outputs', '输出')}</strong><button data-add="${kind}">+ ${tr('Port', '端口')}</button></div>`);
      items.forEach((molecule, index) => {
        const active = kind === state.activeKind && index === state.activeIndex;
        rows.push(`<button class="port-chip ${active ? 'active' : ''}" data-kind="${kind}" data-index="${index}">${tr(kind === 'inputs' ? 'In' : 'Out', kind === 'inputs' ? '输入' : '输出')} ${index + 1}<span>${molecule.atoms.length}${tr(' atoms', ' 原子')}</span><span class="port-remove" data-remove-kind="${kind}" data-remove-index="${index}">×</span></button>`);
      });
      rows.push('</div>');
    }
    host.innerHTML = rows.join('');
    host.querySelectorAll('[data-add]').forEach((button) => button.addEventListener('click', () => {
      const kind = button.dataset.add;
      state[kind].push({ atoms: [], bonds: [] });
      state.activeKind = kind;
      state.activeIndex = state[kind].length - 1;
      state.selected = null; state.bondStart = null;
      render();
    }));
    host.querySelectorAll('[data-kind]').forEach((button) => button.addEventListener('click', (event) => {
      if (event.target.classList.contains('port-remove')) return;
      state.activeKind = button.dataset.kind;
      state.activeIndex = Number(button.dataset.index);
      state.selected = null; state.bondStart = null;
      render();
    }));
    host.querySelectorAll('.port-remove').forEach((button) => button.addEventListener('click', (event) => {
      event.stopPropagation();
      const kind = button.dataset.removeKind;
      if (state[kind].length <= 1) { setStatus(tr('Keep at least one port.', '至少保留一个端口。'), 'bad'); return; }
      state[kind].splice(Number(button.dataset.removeIndex), 1);
      state.activeKind = kind;
      state.activeIndex = Math.min(state.activeIndex, state[kind].length - 1);
      state.selected = null; state.bondStart = null;
      render();
    }));
  };

  const renderPalette = () => {
    const host = document.getElementById('builder-palette');
    host.innerHTML = symbols.map(([type, symbol, name]) =>
      `<button class="atom-chip ${type === state.atomType ? 'active' : ''}" data-type="${type}" title="${name}">${symbol}</button>`).join('');
    host.querySelectorAll('[data-type]').forEach((button) => button.addEventListener('click', () => {
      state.atomType = Number(button.dataset.type);
      state.tool = 'atom';
      state.bondStart = null;
      render();
    }));
  };

  const renderGrid = () => {
    const svg = document.getElementById('builder-grid');
    const molecule = activeMolecule();
    const points = [];
    for (let q = -4; q <= 4; ++q) for (let r = -4; r <= 4; ++r) if (Math.abs(q + r) <= 4) points.push(pixel(q, r));
    if (!points.length) return;
    const minX = Math.min(...points.map((p) => p.x)) - UNIT * 1.5;
    const maxX = Math.max(...points.map((p) => p.x)) + UNIT * 1.5;
    const minY = Math.min(...points.map((p) => p.y)) - UNIT * 1.5;
    const maxY = Math.max(...points.map((p) => p.y)) + UNIT * 1.5;
    svg.setAttribute('viewBox', `${minX} ${minY} ${maxX - minX} ${maxY - minY}`);
    const parts = [`<rect x="${minX}" y="${minY}" width="${maxX - minX}" height="${maxY - minY}" fill="#0d1117"/>`];
    for (let q = -4; q <= 4; ++q) for (let r = -4; r <= 4; ++r) {
      if (Math.abs(q + r) > 4) continue;
      const p = pixel(q, r);
      parts.push(`<polygon data-q="${q}" data-r="${r}" points="${hexPoints(p.x, p.y, UNIT * 0.95)}" fill="#11161e" stroke="#303744" stroke-width="1"/>`);
    }
    const bondColor = (bits) => {
      if (bits & 2) return '#ff6b6b';
      if (bits & 8) return '#f2d14b';
      if (bits & 4) return '#9ca3af';
      return '#c4cedd';
    };
    const bondLine = (bond, selected) => {
      const pa = pixel(bond.from.q, bond.from.r), pb = pixel(bond.to.q, bond.to.r);
      parts.push(`<line x1="${pa.x}" y1="${pa.y}" x2="${pb.x}" y2="${pb.y}" stroke="${selected ? '#f2b84b' : bondColor(bond.bits)}" stroke-width="${selected ? 4 : 3}"/>`);
    };
    molecule.bonds.forEach((bond) => bondLine(bond, state.selected?.bond === bond));
    for (const atom of molecule.atoms) {
      const p = pixel(atom.q, atom.r);
      const selected = state.selected?.atom === atom;
      parts.push(`<circle data-atom-q="${atom.q}" data-atom-r="${atom.r}" cx="${p.x}" cy="${p.y}" r="${UNIT * 0.56}" fill="#1a2230" stroke="${selected ? '#f2b84b' : '#c4cedd'}" stroke-width="${selected ? 3 : 2}"/>`);
      parts.push(`<text x="${p.x}" y="${p.y + 4}" text-anchor="middle" font-size="12" font-family="monospace" fill="#e8edf5">${atomByType.get(atom.type) || '?'}</text>`);
    }
    svg.innerHTML = parts.join('');
    svg.querySelectorAll('polygon[data-q]').forEach((cell) => cell.addEventListener('click', () => onCell(Number(cell.dataset.q), Number(cell.dataset.r))));
    svg.querySelectorAll('circle[data-atom-q]').forEach((circle) => circle.addEventListener('click', (event) => {
      event.stopPropagation();
      onAtom(Number(circle.dataset.atomQ), Number(circle.dataset.atomR));
    }));
  };

  const onCell = (q, r) => {
    const molecule = activeMolecule();
    const atom = atomAt(q, r);
    if (atom) { onAtom(q, r); return; }
    if (state.tool === 'erase') return;
    if (state.tool === 'bond') return;
    molecule.atoms.push({ type: state.atomType, q, r });
    state.selected = { atom: molecule.atoms[molecule.atoms.length - 1] };
    state.bondStart = null;
    setStatus(tr(`Added ${atomByType.get(state.atomType)} at ${q},${r}.`, `已在 ${q},${r} 添加 ${atomByType.get(state.atomType)}。`), 'ok');
    render();
  };

  const onAtom = (q, r) => {
    const molecule = activeMolecule();
    const atom = atomAt(q, r);
    if (!atom) return;
    if (state.tool === 'erase') {
      molecule.atoms = molecule.atoms.filter((item) => item !== atom);
      molecule.bonds = molecule.bonds.filter((bond) =>
        !(bond.from.q === q && bond.from.r === r) && !(bond.to.q === q && bond.to.r === r));
      state.selected = null; state.bondStart = null;
      setStatus(tr(`Removed atom at ${q},${r}.`, `已删除 ${q},${r} 的原子。`), 'ok');
      render();
      return;
    }
    if (state.tool === 'bond') {
      if (!state.bondStart) {
        state.bondStart = atom;
        setStatus(tr('Select the second atom for a bond.', '请选择第二个原子以添加键。'));
      } else if (state.bondStart !== atom) {
        const existing = bondBetween(state.bondStart, atom);
        if (existing) {
          existing.bits = state.bondBits;
          setStatus(tr('Bond type updated.', '已更新键类型。'), 'ok');
        } else {
          molecule.bonds.push({ bits: state.bondBits, from: { q: state.bondStart.q, r: state.bondStart.r }, to: { q, r } });
          setStatus(tr('Bond added.', '已添加键。'), 'ok');
        }
        state.bondStart = null;
      }
      render();
      return;
    }
    state.selected = { atom };
    state.bondStart = null;
    setStatus(tr(`Selected ${atomByType.get(atom.type)} at ${q},${r}.`, `已选择 ${q},${r} 的 ${atomByType.get(atom.type)}。`));
    render();
  };

  const renderProduction = () => {
    const host = document.getElementById('builder-production');
    const enabled = !!state.production;
    const production = state.production || { shrinkLeft: false, shrinkRight: false, isolation: false, chambers: [], conduits: [], vials: [] };
    const rows = [
      `<label class="check"><input id="prod-enabled" type="checkbox" ${enabled ? 'checked' : ''}> ${tr('Enable production metadata', '启用 production metadata')}</label>`,
    ];
    if (enabled) {
      rows.push(`<div class="prod-flags">
        <label class="check"><input data-flag="shrinkLeft" type="checkbox" ${production.shrinkLeft ? 'checked' : ''}> shrinkLeft</label>
        <label class="check"><input data-flag="shrinkRight" type="checkbox" ${production.shrinkRight ? 'checked' : ''}> shrinkRight</label>
        <label class="check"><input data-flag="isolation" type="checkbox" ${production.isolation ? 'checked' : ''}> isolation</label>
      </div>`);
      rows.push(`<h4>${tr('Chambers', '反应室')} <button data-prod-add="chambers">+</button></h4>`);
      production.chambers.forEach((chamber, index) => rows.push(
        `<div class="prod-row"><input data-prod="chambers" data-index="${index}" data-field="q" type="number" value="${chamber.q}"><input data-prod="chambers" data-index="${index}" data-field="r" type="number" value="${chamber.r}"><input data-prod="chambers" data-index="${index}" data-field="type" value="${chamber.type || 'Large'}"><button data-prod-del="chambers" data-index="${index}">×</button></div>`));
      rows.push(`<h4>${tr('Conduits', '导管')} <button data-prod-add="conduits">+</button></h4>`);
      production.conduits.forEach((conduit, index) => rows.push(
        `<div class="prod-row"><input data-prod="conduits" data-index="${index}" data-field="aq" type="number" value="${conduit.a[0]}"><input data-prod="conduits" data-index="${index}" data-field="ar" type="number" value="${conduit.a[1]}"><input data-prod="conduits" data-index="${index}" data-field="bq" type="number" value="${conduit.b[0]}"><input data-prod="conduits" data-index="${index}" data-field="br" type="number" value="${conduit.b[1]}"><input data-prod="conduits" data-index="${index}" data-field="path" value="${(conduit.path || []).map(([q, r]) => `${q},${r}`).join(';')}" placeholder="q,r;q,r"><button data-prod-del="conduits" data-index="${index}">×</button></div>`));
      rows.push(`<h4>${tr('Vials', '瓶')} <button data-prod-add="vials">+</button></h4>`);
      production.vials.forEach((vial, index) => rows.push(
        `<div class="prod-row"><input data-prod="vials" data-index="${index}" data-field="q" type="number" value="${vial.q}"><input data-prod="vials" data-index="${index}" data-field="r" type="number" value="${vial.r}"><label class="check"><input data-prod="vials" data-index="${index}" data-field="top" type="checkbox" ${vial.top ? 'checked' : ''}> top</label><input data-prod="vials" data-index="${index}" data-field="count" type="number" value="${vial.count}"><button data-prod-del="vials" data-index="${index}">×</button></div>`));
    }
    host.innerHTML = rows.join('');
    host.querySelector('#prod-enabled').addEventListener('change', (event) => {
      state.production = event.target.checked ? { shrinkLeft: false, shrinkRight: false, isolation: false, chambers: [], conduits: [], vials: [] } : null;
      render();
    });
    host.querySelectorAll('[data-flag]').forEach((input) => input.addEventListener('change', () => {
      state.production[input.dataset.flag] = input.checked;
      render();
    }));
    host.querySelectorAll('[data-prod-add]').forEach((button) => button.addEventListener('click', () => {
      const kind = button.dataset.prodAdd;
      if (kind === 'chambers') state.production.chambers.push({ q: 0, r: 0, type: 'Large' });
      if (kind === 'conduits') state.production.conduits.push({ a: [0, 0], b: [1, 0], path: [[0, 0], [1, 0]] });
      if (kind === 'vials') state.production.vials.push({ q: 0, r: 0, top: false, count: 1 });
      render();
    }));
    host.querySelectorAll('[data-prod-del]').forEach((button) => button.addEventListener('click', () => {
      state.production[button.dataset.prodDel].splice(Number(button.dataset.index), 1);
      render();
    }));
    host.querySelectorAll('[data-prod]').forEach((input) => input.addEventListener('change', () => {
      const kind = input.dataset.prod;
      const object = state.production[kind][Number(input.dataset.index)];
      const field = input.dataset.field;
      const value = input.type === 'checkbox' ? input.checked : input.type === 'number' ? Number(input.value) : input.value;
      if (kind === 'chambers') object[field] = value;
      else if (kind === 'vials') object[field] = value;
      else if (kind === 'conduits') {
        if (field === 'aq') object.a[0] = value;
        if (field === 'ar') object.a[1] = value;
        if (field === 'bq') object.b[0] = value;
        if (field === 'br') object.b[1] = value;
        if (field === 'path') object.path = String(value).split(';').filter(Boolean).map((pair) => pair.split(',').map(Number));
      }
    }));
  };

  const buildSpec = () => {
    const spec = {
      name: document.getElementById('builder-name').value || 'Custom puzzle',
      outputMultiplier: Number(document.getElementById('builder-multiplier').value) || 1,
      permissionBits: document.getElementById('builder-permissions').value || '0x17c7370f',
      inputs: state.inputs.map((molecule) => ({ atoms: molecule.atoms, bonds: molecule.bonds })),
      outputs: state.outputs.map((molecule) => ({ atoms: molecule.atoms, bonds: molecule.bonds })),
    };
    if (state.production) spec.production = state.production;
    return spec;
  };

  const build = async () => {
    const button = document.getElementById('builder-build');
    button.disabled = true;
    try {
      if (!window.OpusUI) throw new Error(tr('The solver UI is not ready.', '求解器 UI 尚未就绪。'));
      const module = await window.OpusUI.ensureModule();
      const json = JSON.stringify(buildSpec());
      const encoded = new TextEncoder().encode(json);
      const jsonPtr = module._malloc(encoded.length + 1);
      module.HEAPU8.set(encoded, jsonPtr);
      module.HEAPU8[jsonPtr + encoded.length] = 0;
      const resultPtrPtr = module._malloc(4);
      const lengthPtr = module._malloc(4);
      const errorPtrPtr = module._malloc(4);
      try {
        const ok = module.ccall('opusoptima_make_puzzle_json', 'number',
          ['number', 'number', 'number', 'number'],
          [jsonPtr, resultPtrPtr, lengthPtr, errorPtrPtr]);
        const errorPtr = module.HEAPU32[errorPtrPtr >>> 2];
        if (!ok) throw new Error(errorPtr ? module.UTF8ToString(errorPtr) : 'puzzle build failed');
        if (errorPtr) module.ccall('opusoptima_free', null, ['number'], [errorPtr]);
        const resultPtr = module.HEAPU32[resultPtrPtr >>> 2];
        const length = module.HEAPU32[lengthPtr >>> 2];
        const bytes = module.HEAPU8.slice(resultPtr, resultPtr + length);
        module.ccall('opusoptima_free', null, ['number'], [resultPtr]);
        window.OpusUI.loadGeneratedPuzzle(bytes, buildSpec().name);
        const link = document.getElementById('builder-download');
        const blob = new Blob([bytes], { type: 'application/octet-stream' });
        link.href = URL.createObjectURL(blob);
        link.download = `${buildSpec().name}.puzzle`;
        link.classList.remove('disabled');
        setStatus(tr(`Built ${bytes.length} bytes. Loaded into the solver.`, `已构建 ${bytes.length} 字节并加载到求解器。`), 'ok');
      } finally {
        module._free(jsonPtr); module._free(resultPtrPtr); module._free(lengthPtr); module._free(errorPtrPtr);
      }
    } catch (error) {
      setStatus(error?.message || String(error), 'bad');
    } finally {
      button.disabled = false;
    }
  };

  const render = () => {
    document.getElementById('builder-name').value = state.name;
    document.getElementById('builder-multiplier').value = state.multiplier;
    document.getElementById('builder-permissions').value = state.permissions;
    renderPorts();
    renderPalette();
    renderProduction();
    renderGrid();
    const tool = document.getElementById('builder-tool');
    tool.value = state.tool;
    document.getElementById('builder-bond').value = String(state.bondBits);
  };

  // Build the fixed scaffold once; the dynamic areas render below it.
  root.innerHTML = `
    <p class="eyebrow" data-en="Puzzle builder" data-zh="题目构建器">Puzzle builder</p>
    <h2 data-en="Build a full multi-port puzzle" data-zh="构建完整的多端口题目">Build a full multi-port puzzle</h2>
    <p class="fineprint" data-en="Click atoms/bonds on the grid, switch ports, then add production metadata. The builder writes the same JSON schema as the advanced editor." data-zh="在网格上点击原子/键，切换端口，再添加 production metadata；构建器使用与高级编辑器相同的 JSON schema。">Click atoms/bonds on the grid, switch ports, then add production metadata. The builder writes the same JSON schema as the advanced editor.</p>
    <div class="builder-meta">
      <label><span data-en="Name" data-zh="名称">Name</span><input id="builder-name"></label>
      <label><span data-en="Output multiplier" data-zh="输出倍数">Output multiplier</span><input id="builder-multiplier" type="number" min="1" max="12"></label>
      <label><span data-en="Permission bits" data-zh="权限位">Permission bits</span><input id="builder-permissions" spellcheck="false"></label>
    </div>
    <div class="builder-layout">
      <div class="builder-left">
        <div id="builder-ports" class="builder-ports"></div>
        <div class="row">
          <label><span data-en="Tool" data-zh="工具">Tool</span><select id="builder-tool"><option value="atom" data-en="Atom" data-zh="原子">Atom</option><option value="bond" data-en="Bond" data-zh="键">Bond</option><option value="erase" data-en="Erase" data-zh="删除">Erase</option></select></label>
          <label><span data-en="Bond type" data-zh="键类型">Bond type</span><select id="builder-bond"><option value="1">Normal · 1</option><option value="2">Red · 2</option><option value="4">Black · 4</option><option value="8">Yellow · 8</option></select></label>
        </div>
        <div id="builder-palette" class="builder-palette"></div>
        <svg id="builder-grid" role="img" aria-label="Molecule editor"></svg>
      </div>
      <div id="builder-production" class="builder-production"></div>
    </div>
    <div class="row"><button id="builder-build" type="button" data-en="Build and load" data-zh="构建并加载">Build and load</button><a id="builder-download" class="secondary disabled" download data-en="Download .puzzle" data-zh="下载 .puzzle">Download .puzzle</a></div>
    <p id="builder-status" class="status" role="status" data-en="Click a grid cell to add an atom." data-zh="点击网格添加原子。">Click a grid cell to add an atom.</p>`;

  document.getElementById('builder-tool').addEventListener('change', (event) => {
    state.tool = event.target.value; state.bondStart = null; render();
  });
  document.getElementById('builder-bond').addEventListener('change', (event) => {
    state.bondBits = Number(event.target.value); render();
  });
  document.getElementById('builder-name').addEventListener('input', (event) => { state.name = event.target.value; });
  document.getElementById('builder-multiplier').addEventListener('input', (event) => { state.multiplier = Number(event.target.value) || 1; });
  document.getElementById('builder-permissions').addEventListener('input', (event) => { state.permissions = event.target.value; });
  document.getElementById('builder-build').addEventListener('click', build);
  window.addEventListener('opus-lang', () => { render(); OpusI18n.apply(); });
  render();
  OpusI18n.apply();
  window.OpusBuilder = { render };
})();
