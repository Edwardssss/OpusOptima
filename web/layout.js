// Independent machine-layout model/editor. This is not a .puzzle file: the game
// format has no mechanism placement, so layouts are saved as .layout.json for
// planning and preview. The solver still derives the published machine.
(function (global) {
  const TYPES = [
    'arm1', 'piston', 'baron', 'track',
    'glyph-calcification', 'glyph-duplication', 'glyph-bond', 'glyph-unbond',
    'glyph-projection', 'glyph-purification', 'glyph-animismus',
    'glyph-unification', 'glyph-dispersion',
  ];

  const createLayout = () => ({ version: 1, objects: [] });

  const validate = (layout) => {
    if (!layout || typeof layout !== 'object' || !Array.isArray(layout.objects))
      throw new Error('layout must contain an objects array');
    for (const object of layout.objects) {
      if (!object || typeof object.type !== 'string' || !TYPES.includes(object.type))
        throw new Error(`unknown object type: ${object?.type}`);
      if (!Number.isInteger(object.q) || !Number.isInteger(object.r))
        throw new Error('object coordinates must be integers');
      if (object.rotation !== undefined &&
          (!Number.isInteger(object.rotation) || object.rotation < 0 || object.rotation > 5))
        throw new Error('object rotation must be 0..5');
    }
    return layout;
  };

  const addObject = (layout, object) => {
    const next = { type: object.type, q: object.q, r: object.r, rotation: object.rotation || 0 };
    if (object.length) next.length = object.length;
    validate({ version: 1, objects: [next] });
    layout.objects.push(next);
    return next;
  };
  const removeAt = (layout, q, r) => {
    const before = layout.objects.length;
    layout.objects = layout.objects.filter((object) => object.q !== q || object.r !== r);
    return before !== layout.objects.length;
  };
  const toJSON = (layout) => JSON.stringify(validate(layout), null, 2);
  const fromJSON = (text) => validate(JSON.parse(text));
  const objectsAt = (layout, q, r) => layout.objects.filter((object) => object.q === q && object.r === r);

  const api = { TYPES, createLayout, validate, addObject, removeAt, toJSON, fromJSON, objectsAt };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  global.OpusLayout = api;

  if (typeof document === 'undefined') return;

  const svg = document.getElementById('layout-svg');
  if (!svg) return;
  const tool = document.getElementById('layout-tool');
  const rotation = document.getElementById('layout-rotation');
  const deleteButton = document.getElementById('layout-delete');
  const clearButton = document.getElementById('layout-clear');
  const save = document.getElementById('layout-save');
  const load = document.getElementById('layout-load');
  const json = document.getElementById('layout-json');
  const apply = document.getElementById('layout-apply');
  const status = document.getElementById('layout-status');
  const UNIT = 22;
  let layout = createLayout();
  let selected = null;

  const setStatus = (text, kind = '') => {
    status.textContent = text;
    status.className = `status ${kind}`;
  };
  const pixel = (q, r) => ({ x: (q + r / 2) * Math.sqrt(3) * UNIT, y: r * 1.5 * UNIT });
  const hexPoints = (x, y, radius) => {
    const points = [];
    for (let i = 0; i < 6; ++i) {
      const a = Math.PI / 180 * (60 * i - 30);
      points.push(`${(x + radius * Math.cos(a)).toFixed(2)},${(y + radius * Math.sin(a)).toFixed(2)}`);
    }
    return points.join(' ');
  };
  const color = (type) => type === 'arm1' ? '#c4cedd' : type === 'piston' ? '#ffb86b'
    : type === 'baron' ? '#b8a1ff' : type === 'track' ? '#586579' : '#f2b84b';
  const label = (type) => type.startsWith('glyph-') ? type.slice(6, 7).toUpperCase()
    : type === 'track' ? 'T' : type === 'piston' ? 'P' : type === 'baron' ? 'B' : 'A';

  const render = () => {
    const parts = [];
    for (let q = -7; q <= 7; ++q) {
      for (let r = -7; r <= 7; ++r) {
        if (Math.abs(q + r) > 7) continue;
        const p = pixel(q, r);
        const occupied = objectsAt(layout, q, r).length > 0;
        parts.push(`<polygon data-q="${q}" data-r="${r}" points="${hexPoints(p.x, p.y, UNIT * 0.95)}" fill="${occupied ? '#202836' : '#11161e'}" stroke="#303744" stroke-width="1"/>`);
      }
    }
    for (const object of layout.objects) {
      const p = pixel(object.q, object.r);
      const isSelected = object === selected;
      if (object.type === 'track') {
        parts.push(`<polygon data-q="${object.q}" data-r="${object.r}" points="${hexPoints(p.x, p.y, UNIT * 0.92)}" fill="#20242c" stroke="${isSelected ? '#f2b84b' : '#586579'}" stroke-width="2"/>`);
        parts.push(`<text x="${p.x}" y="${p.y + 4}" text-anchor="middle" font-size="12" font-family="monospace" fill="#9aa4b5">T</text>`);
      } else if (object.type.startsWith('glyph-')) {
        parts.push(`<polygon data-q="${object.q}" data-r="${object.r}" points="${hexPoints(p.x, p.y, UNIT * 0.82)}" fill="#2b2415" stroke="${isSelected ? '#fff' : '#f2b84b'}" stroke-width="2"/>`);
        parts.push(`<text x="${p.x}" y="${p.y + 5}" text-anchor="middle" font-size="12" font-weight="700" font-family="monospace" fill="#f2b84b">${label(object.type)}</text>`);
      } else {
        const angle = (object.rotation || 0) * Math.PI / 3 - Math.PI / 2;
        const length = UNIT * 1.5;
        parts.push(`<line x1="${p.x}" y1="${p.y}" x2="${p.x + Math.cos(angle) * length}" y2="${p.y + Math.sin(angle) * length}" stroke="${isSelected ? '#fff' : color(object.type)}" stroke-width="4" stroke-linecap="round"/>`);
        parts.push(`<circle data-q="${object.q}" data-r="${object.r}" cx="${p.x}" cy="${p.y}" r="${UNIT * 0.62}" fill="#171d28" stroke="${isSelected ? '#fff' : color(object.type)}" stroke-width="2"/>`);
        parts.push(`<text x="${p.x}" y="${p.y + 4}" text-anchor="middle" font-size="9" font-family="monospace" fill="${color(object.type)}">${label(object.type)}</text>`);
      }
    }
    svg.setAttribute('viewBox', `${-7 * UNIT * 2.2} ${-7 * UNIT * 2.2} ${14 * UNIT * 2.2} ${14 * UNIT * 2.2}`);
    svg.innerHTML = parts.join('');
    for (const cell of svg.querySelectorAll('polygon[data-q], circle[data-q]')) {
      cell.addEventListener('click', (event) => {
        event.stopPropagation();
        const q = Number(cell.dataset.q), r = Number(cell.dataset.r);
        const hit = objectsAt(layout, q, r)[0];
        if (tool.value === 'erase') {
          if (hit) {
            layout.objects = layout.objects.filter((object) => object !== hit);
            if (selected === hit) selected = null;
            setStatus(`Erased ${q},${r}.`);
            render();
          }
          return;
        }
        if (hit) { selected = hit; setStatus(`Selected ${hit.type} at ${q},${r}.`); render(); return; }
        selected = addObject(layout, { type: tool.value, q, r, rotation: Number(rotation.value) });
        setStatus(`Placed ${selected.type} at ${q},${r}.`);
        render();
      });
    }
  };

  tool.addEventListener('change', () => setStatus(`Tool: ${tool.value}.`));
  rotation.addEventListener('change', () => {
    if (selected) { selected.rotation = Number(rotation.value); render(); }
  });
  deleteButton.addEventListener('click', () => {
    if (!selected) return;
    layout.objects = layout.objects.filter((object) => object !== selected);
    selected = null;
    setStatus('Deleted selected object.');
    render();
  });
  clearButton.addEventListener('click', () => { layout = createLayout(); selected = null; setStatus('Layout cleared.'); render(); });
  save.addEventListener('click', () => {
    const blob = new Blob([toJSON(layout)], { type: 'application/json' });
    save.href = URL.createObjectURL(blob);
    save.download = 'machine.layout.json';
    save.classList.remove('disabled');
  });
  load.addEventListener('change', async () => {
    try {
      layout = fromJSON(await load.files[0].text()); selected = null; setStatus(`Loaded ${layout.objects.length} objects.`, 'ok'); render();
    } catch (error) { setStatus(String(error.message || error), 'bad'); }
  });
  apply.addEventListener('click', () => {
    try { layout = fromJSON(json.value); selected = null; setStatus(`Applied ${layout.objects.length} objects.`, 'ok'); render(); }
    catch (error) { setStatus(String(error.message || error), 'bad'); }
  });
  render();
})(typeof globalThis !== 'undefined' ? globalThis : this);
