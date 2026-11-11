const fs = require('node:fs');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const root = path.resolve(__dirname, '..');
const target = `${process.platform}-${process.arch}`;
if (!['darwin-arm64', 'darwin-x64', 'linux-x64', 'linux-arm64'].includes(target)) {
  throw new Error(`Unsupported packaging host: ${target}. Build on the target platform.`);
}
const bin = path.join(root, 'bin');
fs.mkdirSync(bin, { recursive: true });
const compiler = path.join(bin, 'mylang');
const sources = ['main', 'lexer', 'parser', 'codegen', 'pipeline'].map(name => path.join(root, '../src', `${name}.c`));
const version = fs.readFileSync(path.join(root, '../VERSION'), 'utf8').trim();
// Build from source every time: never distribute a stale workspace executable.
execFileSync('cc', ['-std=c99', '-O2', '-Wall', '-Wextra', '-D_POSIX_C_SOURCE=200809L', `-DMYLANG_VERSION="${version}"`, ...sources, '-o', compiler, '-lm'], { stdio: 'inherit', env: { ...process.env, ...(process.platform === 'darwin' ? { MACOSX_DEPLOYMENT_TARGET: '14.0' } : {}) } });
fs.chmodSync(compiler, 0o755);
execFileSync(compiler, ['check', path.join(root, '../examples/hello.vibes')], { stdio: 'inherit' });
fs.writeFileSync(path.join(bin, 'build.json'), JSON.stringify({ target, compilerVersion: version }, null, 2) + '\n');
const manifest = require('../package.json');
const vsce = require.resolve('@vscode/vsce/vsce');
execFileSync(process.execPath, [vsce, 'package', '--target', target, '--out', `vibes-vscode-${manifest.version}-${target}.vsix`], { cwd: root, stdio: 'inherit' });
