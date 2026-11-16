// Exercise the independent machine-layout model (no DOM required).
const layout = require('./layout.js');

const model = layout.createLayout();
layout.addObject(model, { type: 'arm1', q: 0, r: 0, rotation: 2 });
layout.addObject(model, { type: 'piston', q: 1, r: 0, rotation: 4 });
layout.addObject(model, { type: 'glyph-calcification', q: 2, r: -1, rotation: 0 });
layout.addObject(model, { type: 'track', q: 3, r: -1, rotation: 0, cells: [[3, -1], [4, -1], [4, 0]] });
const roundTrip = layout.fromJSON(layout.toJSON(model));
if (roundTrip.objects.length !== 4) throw new Error('layout round-trip lost objects');
if (!layout.objectsAt(roundTrip, 2, -1).some((object) => object.type === 'glyph-calcification'))
  throw new Error('glyph placement missing after round-trip');
if (!layout.objectsAt(roundTrip, 4, 0).some((object) => object.type === 'track'))
  throw new Error('track path cells missing after round-trip');
if (!layout.removeAt(roundTrip, 1, 0) || roundTrip.objects.length !== 3)
  throw new Error('layout removal failed');
console.log(`layout: ${roundTrip.objects.length} objects, arm/piston/glyph/track round-trip passed`);
