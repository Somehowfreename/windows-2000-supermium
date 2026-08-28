const fs = await import("node:fs");
const path = await import("node:path");
const crypto = await import("node:crypto");

const rootDirectory = process.argv[2];
const outputPath = process.argv[3];
if (!rootDirectory || !outputPath) {
  throw new Error("usage: generate_sha256_manifest.js ROOT_DIRECTORY OUTPUT_FILE");
}
const absoluteOutput = path.resolve(outputPath).toLowerCase();

function filesBelow(directory) {
  const files = [];
  for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
    const full = path.join(directory, entry.name);
    if (entry.isDirectory()) files.push(...filesBelow(full));
    else if (entry.isFile() && path.resolve(full).toLowerCase() !== absoluteOutput) files.push(full);
  }
  return files;
}

const files = filesBelow(rootDirectory).sort((a, b) => a.localeCompare(b));
const lines = files.map((file) => {
  const digest = crypto.createHash("sha256").update(fs.readFileSync(file)).digest("hex").toUpperCase();
  const relative = path.relative(rootDirectory, file).replaceAll("\\", "/");
  return `${digest} *${relative}`;
});
fs.writeFileSync(outputPath, `${lines.join("\r\n")}\r\n`, "ascii");
console.log(JSON.stringify({ rootDirectory, outputPath, fileCount: files.length }));
