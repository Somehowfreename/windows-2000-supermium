const fs = await import("node:fs");

const manifestPath = process.argv[2];
const outputPath = process.argv[3];
if (!manifestPath || !outputPath) {
  throw new Error("usage: render_certificate_manifest_text.js MANIFEST_JSON OUTPUT_TXT");
}

const manifest = JSON.parse(fs.readFileSync(manifestPath, "utf8"));
const lines = [
  "Mozilla-derived TLS server-authentication root certificates",
  "===========================================================",
  "",
  `Source: ${manifest.source.url}`,
  `Source SHA-256: ${manifest.source.sourceSha256}`,
  `Certificate count: ${manifest.certificateCount}`,
  "",
];
for (const [index, certificate] of manifest.certificates.entries()) {
  lines.push(`${index + 1}. ${certificate.subject.replaceAll("\n", ", ")}`);
  lines.push(`   File: ${certificate.filename}`);
  lines.push(`   SHA-256: ${certificate.sha256}`);
  lines.push(`   SHA-1: ${certificate.sha1}`);
  lines.push(`   Valid: ${certificate.validFrom} through ${certificate.validTo}`);
  lines.push("");
}
fs.writeFileSync(outputPath, `${lines.join("\r\n")}\r\n`, "utf8");
console.log(JSON.stringify({ outputPath, certificateCount: manifest.certificateCount }));
