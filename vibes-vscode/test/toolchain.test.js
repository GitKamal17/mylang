const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const { inspect } = require('../toolchain');
const { createProject } = require('../project');
const success = async (cmd, args) => ({ stdout: cmd === 'compiler' ? (args[0] === 'emit' ? '#include "raylib.h"' : 'mylang 0.1.0') : 'clang' });
test('missing compiler gives actionable message', async () => {
  const r = await inspect('compiler', {}, async () => { throw new Error('ENOENT'); });
  assert.equal(r.ok, false); assert.match(r.message, /Reinstall/);
});
test('missing clang gives setup instructions', async () => {
  const r = await inspect('compiler', {}, async (cmd, args) => { if (cmd === 'clang') throw new Error('ENOENT'); return success(cmd, args); });
  assert.equal(r.ok, false); assert.match(r.message, /toolchain/);
});
test('plain programs do not require Raylib', async () => {
  const calls = [];
  const r = await inspect('compiler', {}, async (cmd, args) => { calls.push(args); return success(cmd, args); });
  assert.ok(r.ok); assert.ok(!calls.some(args => args.includes('-lraylib')));
});
test('imported game detection probes Raylib and reports link failure', async () => {
  const r = await inspect('compiler', { source: '/tmp/game.vibes' }, async (cmd, args) => {
    if (args.includes('-lraylib')) throw Object.assign(new Error('link failed'), { stderr: 'library missing' });
    return success(cmd, args);
  });
  assert.equal(r.game, true); assert.equal(r.ok, false); assert.match(r.message, /Raylib/);
});
test('source errors remain visible', async () => {
  const r = await inspect('compiler', { source: '/tmp/bad.vibes' }, async (cmd, args) => {
    if (args[0] === 'emit') throw Object.assign(new Error('parse failed'), { stderr: 'Error [1:1]: invalid' });
    return success(cmd, args);
  });
  assert.equal(r.ok, false); assert.match(r.details.join(' '), /Error \[1:1\]/);
});
test('creates both templates, refuses traversal and existing folders', async () => {
  const tmp = await fs.mkdtemp(path.join(os.tmpdir(), 'vibes-project-'));
  try {
    for (const kind of ['program', 'game']) {
      const directory = await createProject(tmp, kind, kind);
      assert.match(await fs.readFile(path.join(directory, 'main.vibes'), 'utf8'), kind === 'game' ? /window/ : /Hello, Vibes!/);
    }
    await assert.rejects(createProject(tmp, 'program', 'program'), { code: 'EEXIST' });
    await assert.rejects(createProject(tmp, '../escape', 'program'));
  } finally { await fs.rm(tmp, { recursive: true, force: true }); }
});
test('real compiler and Clang work outside the checkout', async () => {
  const tmp = await fs.mkdtemp(path.join(os.tmpdir(), 'vibes-real-'));
  try {
    const directory = await createProject(tmp, 'hello', 'program');
    const compiler = path.resolve(__dirname, '../../mylang');
    const r = await inspect(compiler, { source: path.join(directory, 'main.vibes') });
    assert.equal(r.ok, true, JSON.stringify(r));
  } finally { await fs.rm(tmp, { recursive: true, force: true }); }
});
