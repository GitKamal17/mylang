const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { execFile } = require('node:child_process');
const { promisify } = require('node:util');
const run = promisify(execFile);
const options = { timeout: 20000, maxBuffer: 16 * 1024 * 1024 };

function installAdvice(game, platform = process.platform) {
  if (platform === 'darwin') return game
    ? 'Install Raylib with Homebrew: brew install raylib. Then retry.'
    : 'Install Apple Command Line Tools: xcode-select --install. Complete installation, then retry.';
  return game
    ? 'Install the Raylib development headers and library for your Linux distribution. See https://github.com/raysan5/raylib/wiki/Working-on-GNU-Linux. Then retry.'
    : 'Install Clang and system development tools. On Ubuntu/Debian: sudo apt install clang build-essential. Then retry.';
}

async function inspect(compiler, { source, game = false } = {}, invoke = run) {
  const report = { ok: false, compiler, game, details: [] };
  try {
    const version = await invoke(compiler, ['--version'], options);
    if (!/^mylang\s/m.test(version.stdout)) throw new Error('Executable did not identify itself as MyLang.');
    report.details.push(version.stdout.trim());
  } catch (error) {
    report.message = 'Cannot run the MyLang compiler. Reinstall the extension for your platform, or correct vibes.compilerPath.';
    report.details.push(error.message); return report;
  }
  if (source) {
    try {
      const emitted = await invoke(compiler, ['emit', source], options);
      // Inspect generated C so imports and comments cannot misclassify a game.
      report.game = /^#include\s+[<"]raylib\.h[>"]/m.test(emitted.stdout);
    } catch (error) {
      report.message = 'MyLang could not generate C. Fix the source errors shown in the MyLang output.';
      report.details.push(error.stderr || error.message); return report;
    }
  }
  let temporary;
  try {
    await invoke('clang', ['--version'], options);
    temporary = await fs.promises.mkdtemp(path.join(os.tmpdir(), 'mylang-check-'));
    const input = path.join(temporary, 'check.c');
    const output = path.join(temporary, 'check');
    await fs.promises.writeFile(input, '#include <stdio.h>\nint main(void) { puts("ok"); return 0; }\n');
    await invoke('clang', ['-std=c99', input, '-o', output, '-lm'], options);
    report.details.push('Clang and system headers: ready');
    if (report.game) {
      await fs.promises.writeFile(input, '#include "raylib.h"\nint main(void) { return GetRandomValue(0, 1); }\n');
      const args = ['-std=c99', input, '-o', output];
      // Match the compiler's Raylib search order exactly.
      const root = ['/opt/homebrew', '/usr/local'].find(p => fs.existsSync(path.join(p, 'include/raylib.h')));
      if (root) args.push(`-I${root}/include`, `-L${root}/lib`);
      args.push('-lraylib', '-lm');
      try { await invoke('clang', args, options); }
      catch (error) {
        report.message = `Raylib could not be compiled and linked. ${installAdvice(true)}`;
        report.details.push(error.stderr || error.message); return report;
      }
      report.details.push('Raylib headers and linking: ready');
    }
    report.ok = true;
    report.message = report.game ? 'Ready to build and run MyLang games.' : 'Ready to build and run MyLang programs.';
  } catch (error) {
    report.message = `The C toolchain is unavailable or incomplete. ${installAdvice(false)}`;
    report.details.push(error.stderr || error.message);
  } finally {
    if (temporary) await fs.promises.rm(temporary, { recursive: true, force: true });
  }
  return report;
}
module.exports = { inspect, installAdvice };
