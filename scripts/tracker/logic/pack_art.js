// Packs the tracker's images into one atlas for the game's tracker screen (docs/TRACKER.md, "In the
// game"), so the game needs no PNG decoder:
//
//   node pack_art.js <images-map.json> <images dir> <out dir>
//
// images-map.json is the tracker's own image table (src/ui/images.js with each import replaced by its
// file name; build_logic.sh makes it): nested keys down to a file, e.g. ITEMS -> "Progressive Sword" ->
// "2" -> "item21_2_a.png". Writes <out dir>/art.rgba ("CTA1", width and height as little-endian u32,
// then RGBA8 rows) and <out dir>/art.txt (one "<keys joined by '/'>\t<x>\t<y>\t<w>\t<h>" per image).
// The island and dungeon detail charts (325x325 each) are left out. Uses sharp, which the tracker's
// own dependencies bring (run from its checkout).
const fs = require('fs');
const path = require('path');
const sharp = require('sharp');

const SKIP = new Set(['ISLAND_CHART_BACKGROUNDS', 'DUNGEON_CHART_BACKGROUNDS', 'EMPTY_BACKGROUND']);
const ATLAS_WIDTH = 1024;

function walk(node, keys, out) {
  if (node !== null && typeof node === 'object') {
    Object.entries(node).forEach(([key, value]) => walk(value, [...keys, key], out));
  } else {
    out.push([keys.join('/'), node]);
  }
}

async function main() {
  const [mapFile, imagesDir, outDir] = process.argv.slice(2);
  const table = JSON.parse(fs.readFileSync(mapFile, 'utf8'));
  const entries = [];
  Object.entries(table).forEach(([key, value]) => {
    if (!SKIP.has(key)) {
      walk(value, [key], entries);
    }
  });

  const files = new Map();
  for (const [, name] of entries) {
    if (!files.has(name)) {
      const { data, info } = await sharp(path.join(imagesDir, name))
        .ensureAlpha().raw().toBuffer({ resolveWithObject: true });
      files.set(name, { data, width: info.width, height: info.height });
    }
  }

  // shelf packing, tallest first, 1 px apart (no bleeding between neighbours when scaled)
  const order = [...files.keys()].sort((a, b) => (
    files.get(b).height - files.get(a).height
    || files.get(b).width - files.get(a).width
    || (a < b ? -1 : a > b ? 1 : 0)));
  const place = new Map();
  let x = 0;
  let y = 0;
  let shelf = 0;
  order.forEach((name) => {
    const im = files.get(name);
    if (x + im.width > ATLAS_WIDTH) {
      x = 0;
      y += shelf + 1;
      shelf = 0;
    }
    place.set(name, [x, y]);
    x += im.width + 1;
    shelf = Math.max(shelf, im.height);
  });
  const height = y + shelf;
  const atlas = Buffer.alloc(ATLAS_WIDTH * height * 4);
  place.forEach(([px, py], name) => {
    const im = files.get(name);
    for (let row = 0; row < im.height; row += 1) {
      im.data.copy(atlas, ((py + row) * ATLAS_WIDTH + px) * 4, row * im.width * 4, (row + 1) * im.width * 4);
    }
  });

  fs.mkdirSync(outDir, { recursive: true });
  const header = Buffer.alloc(12);
  header.write('CTA1', 0, 'ascii');
  header.writeUInt32LE(ATLAS_WIDTH, 4);
  header.writeUInt32LE(height, 8);
  fs.writeFileSync(path.join(outDir, 'art.rgba'), Buffer.concat([header, atlas]));
  fs.writeFileSync(path.join(outDir, 'art.txt'), entries.map(([key, name]) => {
    const [px, py] = place.get(name);
    const im = files.get(name);
    return `${key}\t${px}\t${py}\t${im.width}\t${im.height}\n`;
  }).join(''));
  console.log(`pack_art: ${files.size} images, ${entries.length} keys, atlas ${ATLAS_WIDTH}x${height} in ${outDir}`);
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
