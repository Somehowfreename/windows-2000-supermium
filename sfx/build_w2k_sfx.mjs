import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';

function fail(message) { console.error(message); process.exit(1); }
function walk(root, dir = root, out = []) {
  for (const entry of fs.readdirSync(dir, { withFileTypes: true }).sort((a,b) => a.name.localeCompare(b.name))) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) walk(root, full, out);
    else if (entry.isFile()) out.push(path.relative(root, full));
  }
  return out;
}
function ddfQuote(value) { return String.fromCharCode(34) + String(value).replaceAll(String.fromCharCode(34), String.fromCharCode(34)+String.fromCharCode(34)) + String.fromCharCode(34); }
function crc32(buffer) {
  let c = 0xffffffff;
  for (const byte of buffer) {
    c ^= byte;
    for (let j = 0; j < 8; j++) c = (c >>> 1) ^ ((c & 1) ? 0xedb88320 : 0);
  }
  return (c ^ 0xffffffff) >>> 0;
}
function fixedAscii(buffer, offset, length, value, label) {
  if (!/^[\x20-\x7e]+$/.test(value)) fail(label + ' must use printable ASCII only.');
  const bytes = Buffer.from(value, 'ascii');
  if (bytes.length >= length) fail(label + ' is too long.');
  bytes.copy(buffer, offset);
}

const [stubPath, inputDirectory, outputPath, folderName, displayName] = process.argv.slice(2);
if (!stubPath || !inputDirectory || !outputPath || !folderName || !displayName) fail('Usage: node build_w2k_sfx.mjs <stub.exe> <input-dir> <output.exe> <folder-name> <display-name>');
if (!fs.statSync(stubPath).isFile()) fail('SFX stub not found: ' + stubPath);
if (!fs.statSync(inputDirectory).isDirectory()) fail('Input directory not found: ' + inputDirectory);
if (/[\\/:*?"<>|]/.test(folderName) || folderName === '.' || folderName === '..') fail('Unsafe folder name.');

const files = walk(inputDirectory);
if (!files.length) fail('Input directory is empty.');
const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'w2ksfx-'));
try {
  const ddfPath = path.join(temp, 'payload.ddf');
  const cabPath = path.join(temp, 'payload.cab');
  const lines = [
    '.OPTION EXPLICIT',
    '.Set Cabinet=on',
    '.Set Compress=on',
    '.Set CompressionType=LZX',
    '.Set CompressionMemory=21',
    '.Set MaxDiskSize=0',
    '.Set MaxCabinetSize=0',
    '.Set CabinetNameTemplate=payload.cab',
    '.Set DiskDirectoryTemplate=' + ddfQuote(temp),
    '.Set UniqueFiles=off'
  ];
  let currentDir = null;
  for (const relative of files) {
    const relDir = path.dirname(relative) === '.' ? '' : path.dirname(relative);
    if (relDir !== currentDir) { lines.push('.Set DestinationDir=' + ddfQuote(relDir)); currentDir = relDir; }
    lines.push(ddfQuote(path.join(inputDirectory, relative)) + ' ' + ddfQuote(path.basename(relative)));
  }
  fs.writeFileSync(ddfPath, lines.join('\r\n') + '\r\n', 'utf8');
  const makecab = path.join(process.env.SystemRoot || 'C:\\Windows', 'System32', 'makecab.exe');
  const result = spawnSync(makecab, ['/F', ddfPath], { cwd: temp, windowsHide: true, encoding: 'utf8', maxBuffer: 32 * 1024 * 1024 });
  if (result.status !== 0 || !fs.existsSync(cabPath)) fail('makecab failed (' + result.status + '): ' + (result.stderr || result.stdout));
  const cabinet = fs.readFileSync(cabPath);
  if (cabinet.length > 0xffffffff) fail('CAB payload is too large.');
  const footer = Buffer.alloc(208);
  footer.write('W2KSFX1!', 0, 'ascii');
  footer.writeUInt32LE(cabinet.length >>> 0, 8);
  footer.writeUInt32LE(crc32(cabinet), 12);
  fixedAscii(footer, 16, 96, folderName, 'Folder name');
  fixedAscii(footer, 112, 96, displayName, 'Display name');
  fs.mkdirSync(path.dirname(outputPath), { recursive: true });
  fs.copyFileSync(stubPath, outputPath);
  fs.appendFileSync(outputPath, cabinet);
  fs.appendFileSync(outputPath, footer);
  const built = fs.readFileSync(outputPath);
  const crypto = await import('node:crypto');
  console.log(JSON.stringify({ output: outputPath, files: files.length, stubBytes: fs.statSync(stubPath).size, cabinetBytes: cabinet.length, outputBytes: built.length, cabinetCrc32: footer.readUInt32LE(12).toString(16).padStart(8,'0'), sha256: crypto.createHash('sha256').update(built).digest('hex') }, null, 2));
} finally {
  fs.rmSync(temp, { recursive: true, force: true });
}
