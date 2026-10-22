// Writes the tracker's image table as JSON for pack_art.js (docs/TRACKER.md, "In the game"):
//
//   node images_map.js <tracker checkout> <out.json>
//
// src/ui/images.js lists every image as a dynamic import under constant keys (LogicHelper.ITEMS.TELESCOPE
// and so on); this takes that object, puts each image's file name in place of its import, and evaluates
// it with the tracker's LogicHelper (bundled by esbuild, which the caller has in PATH through npx).
const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const [checkout, outFile] = process.argv.slice(2);
const source = fs.readFileSync(path.join(checkout, 'src/ui/images.js'), 'utf8');
const start = source.indexOf('return {', source.indexOf('static get #IMAGE_IMPORTS()'));
if (start < 0) {
  throw new Error('images_map: no #IMAGE_IMPORTS in src/ui/images.js');
}
let depth = 0;
let end = -1;
for (let i = start + 'return '.length; i < source.length && end < 0; i += 1) {
  if (source[i] === '{') {
    depth += 1;
  } else if (source[i] === '}') {
    depth -= 1;
    if (depth === 0) {
      end = i + 1;
    }
  }
}
const table = source
  .slice(start + 'return '.length, end)
  .replace(/import\('\.\.\/images\/([^']+)'\)/g, "'$1'");
const entry = path.join(checkout, 'src/cos-images-map.js');
fs.writeFileSync(entry, `import LogicHelper from './services/logic-helper';\n\nexport default ${table};\n`);
const bundle = path.join(checkout, 'cos-images-map.cjs');
execFileSync('npx', ['--yes', 'esbuild@0.24.0', entry, '--bundle', '--platform=node', '--format=cjs',
  `--inject:${path.join(checkout, 'src/cos-logic-buffer.js')}`, `--outfile=${bundle}`, '--log-level=warning'],
{ stdio: 'inherit', cwd: checkout });
// eslint-disable-next-line import/no-dynamic-require, global-require
fs.writeFileSync(outFile, JSON.stringify(require(bundle).default));
