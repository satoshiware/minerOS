const fs = require('fs');
const path = require('path');

const versionFile = path.join(__dirname, '..', '..', '..', 'version.txt');
const version = fs.readFileSync(versionFile, 'utf8').trim();

const outputPath = path.join(__dirname, 'dist', 'axe-os', 'version.txt');
fs.writeFileSync(outputPath, version);

console.log(`Generated ${outputPath} with version ${version}`);
