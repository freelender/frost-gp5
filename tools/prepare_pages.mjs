// Stage the public editor and an optional verified GitHub Release firmware.
// This creates local files only; it never publishes or accesses a pedal.
import { readFile, writeFile, cp, mkdir, access } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { webcrypto } from 'node:crypto';
import { identifyVerified } from '../web/firmware.js';
globalThis.crypto ??= webcrypto;
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

export async function preparePages({ out, firmware, checksums, version }) {
  if (!out) throw new Error('--out is required');
  const target = path.resolve(out), relative = path.relative(root, target);
  if (!relative || (!relative.startsWith('..' + path.sep) && relative !== '..' && !path.isAbsolute(relative)) ||
      !path.relative(target, root).startsWith('..' + path.sep))
    throw new Error('Choose an output outside the source repository and its parents');
  let exists = false;
  try { await access(target); exists = true; } catch (e) { if (e.code !== 'ENOENT') throw e; }
  if (exists) throw new Error('Output already exists; choose a fresh directory');
  let verified = null;
  if (firmware || checksums || version) {
    if (!firmware || !checksums || !/^\d+\.\d+\.\d+(?:-beta\.\d+)?$/.test(version || ''))
      throw new Error('--firmware, --checksums and --version must be supplied together');
    const name = `Frost_GP5_${version}.bin`;
    if (path.basename(firmware) !== name) throw new Error('Release filename must match the requested version');
    const bytes = new Uint8Array(await readFile(firmware));
    const { id, sha } = await identifyVerified(bytes);
    if (id.kind !== 'frost' || id.version !== version || id.lines.map(x => x.name).join(',') !== 'dsp,dsp-data,app')
      throw new Error('Release firmware identity, version or sections do not match');
    const rows = (await readFile(checksums, 'utf8')).split(/\r?\n/).filter(Boolean)
      .map(line => /^([a-fA-F0-9]{64})\s+\*?(.+)$/.exec(line));
    const matches = rows.filter(row => row && row[2] === name);
    if (matches.length !== 1 || matches[0][1].toLowerCase() !== sha)
      throw new Error('Release firmware checksum is missing, duplicated or mismatched');
    verified = { name, bytes, sha };
  }
  await mkdir(target, { recursive: true });
  await cp(path.join(root, 'web'), target, { recursive: true });
  await writeFile(path.join(target, '.nojekyll'), '');
  for (const name of ['LICENSE', 'THIRD_PARTY_NOTICES.md']) await cp(path.join(root, name), path.join(target, name));
  const manifest = JSON.parse(await readFile(path.join(target, 'firmware/manifest.json'), 'utf8'));
  const entry = manifest.find(row => row.id === 'frost');
  if (!entry) throw new Error('Frost manifest entry missing');
  if (verified) {
    await writeFile(path.join(target, 'firmware', verified.name), verified.bytes);
    Object.assign(entry, { version, file: verified.name, sha256: verified.sha });
  } else Object.assign(entry, { file: '', sha256: '' });
  await writeFile(path.join(target, 'firmware/manifest.json'), JSON.stringify(manifest, null, 2) + '\n');
  return { output: target, firmware: verified?.name ?? null, sha256: verified?.sha ?? null };
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  try {
    const options = {}, names = { '--out': 'out', '--firmware': 'firmware', '--checksums': 'checksums', '--version': 'version' };
    for (let i = 2; i < process.argv.length; i += 2) {
      if (!names[process.argv[i]] || !process.argv[i + 1]) throw new Error('Expected --out DIR [--firmware BIN --checksums FILE --version VERSION]');
      options[names[process.argv[i]]] = process.argv[i + 1];
    }
    console.log(JSON.stringify(await preparePages(options)));
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
