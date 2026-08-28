const fs = await import("node:fs");
const path = await import("node:path");
const crypto = await import("node:crypto");

const inputPath = process.argv[2];
const outputDirectory = process.argv[3];
const manifestPath = process.argv[4];
if (!inputPath || !outputDirectory || !manifestPath) {
  throw new Error("usage: split_mozilla_ca_bundle.js INPUT_PEM OUTPUT_DIRECTORY MANIFEST_JSON");
}

if (fs.existsSync(outputDirectory) && fs.readdirSync(outputDirectory).length !== 0) {
  throw new Error(`Refusing to write into non-empty directory: ${outputDirectory}`);
}
fs.mkdirSync(outputDirectory, { recursive: true });

const bytes = fs.readFileSync(inputPath);
const text = bytes.toString("ascii");
const blocks = [...text.matchAll(/-----BEGIN CERTIFICATE-----[\s\S]*?-----END CERTIFICATE-----/g)]
  .map((match) => match[0]);
if (blocks.length === 0) throw new Error("No PEM certificate blocks found");

const certificates = [];
const seen = new Set();
for (let index = 0; index < blocks.length; ++index) {
  const certificate = new crypto.X509Certificate(blocks[index]);
  const sha256 = certificate.fingerprint256.replaceAll(":", "").toUpperCase();
  const sha1 = certificate.fingerprint.replaceAll(":", "").toUpperCase();
  if (seen.has(sha256)) throw new Error(`Duplicate certificate fingerprint: ${sha256}`);
  seen.add(sha256);
  const filename = `${String(index + 1).padStart(3, "0")}-${sha256}.cer`;
  fs.writeFileSync(path.join(outputDirectory, filename), certificate.raw);
  certificates.push({
    filename,
    sha256,
    sha1,
    subject: certificate.subject,
    issuer: certificate.issuer,
    serialNumber: certificate.serialNumber,
    validFrom: certificate.validFrom,
    validTo: certificate.validTo,
    ca: certificate.ca,
    derBytes: certificate.raw.length,
  });
}

const manifest = {
  generatedAt: new Date().toISOString(),
  source: {
    url: "https://curl.se/ca/cacert.pem",
    documentation: "https://curl.se/docs/caextract.html",
    sourceFile: inputPath,
    sourceBytes: bytes.length,
    sourceSha256: crypto.createHash("sha256").update(bytes).digest("hex").toUpperCase(),
  },
  certificateCount: certificates.length,
  certificates,
};
fs.writeFileSync(manifestPath, `${JSON.stringify(manifest, null, 2)}\n`);
console.log(JSON.stringify({
  certificateCount: certificates.length,
  sourceBytes: bytes.length,
  sourceSha256: manifest.source.sourceSha256,
  outputDirectory,
  manifestPath,
}, null, 2));
