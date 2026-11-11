const { test, beforeEach } = require('node:test');
const assert = require('node:assert/strict');
const Module = require('node:module');
const path = require('node:path');
const fs = require('node:fs');
const bundled = path.resolve(__dirname, '../bin/mylang');
const defaultCompiler = fallback => fs.existsSync(bundled) ? bundled : fallback;
let calls, config, folder;
const document = () => ({ languageId: 'vibes', uri: { scheme: 'file', fsPath: '/tmp/my game$(touch pwned).vibes' }, isDirty: false });
const vscode = {
  workspace: { isTrusted: true, getWorkspaceFolder: () => folder, getConfiguration: () => ({ get: () => config }) },
  window: { activeTextEditor: null, showWarningMessage: m => calls.push(m), showErrorMessage: m => calls.push(m) },
  commands: { registerCommand: (id, callback) => ({ id, callback }) },
  tasks: { executeTask: async t => calls.push(t) },
  ProcessExecution: class { constructor(command, args, options) { Object.assign(this, { command, args, options }); } },
  Task: class { constructor(definition, scope, name, source, execution) { Object.assign(this, { definition, scope, name, source, execution }); } },
  TaskScope: { Global: 1 }, TaskRevealKind: { Always: 1 }, TaskPanelKind: { Dedicated: 2 }
};
const original = Module._load;
Module._load = function(id, ...args) { return id === 'vscode' ? vscode : original.call(this, id, ...args); };
const toolchain = require('../toolchain');
toolchain.inspect = async () => ({ ok: true });
const extension = require('../extension');
Module._load = original;
beforeEach(() => { calls = []; config = ''; folder = undefined; vscode.workspace.isTrusted = true; vscode.window.activeTextEditor = { document: document() }; });
test('registers six commands and output cleanup', () => { const context = { subscriptions: [] }; extension.activate(context); assert.equal(context.subscriptions.length, 7); });
test('passes hostile filenames as a single process argument', async () => { await extension.execute('run'); assert.deepEqual(calls[0].execution.args, ['run', document().uri.fsPath]); assert.equal(calls[0].execution.options.cwd, '/tmp'); });
test('blocks untrusted workspace execution', async () => { vscode.workspace.isTrusted = false; await extension.execute('run'); assert.match(calls[0], /Trust/); });
test('blocks other languages', async () => { vscode.window.activeTextEditor.document.languageId = 'javascript'; await extension.execute('run'); assert.match(calls[0], /Open a/); });
test('blocks untitled files', async () => { vscode.window.activeTextEditor.document.isUntitled = true; await extension.execute('run'); assert.match(calls[0], /Save/); });
test('canceled save never runs', async () => { Object.assign(vscode.window.activeTextEditor.document, { isDirty: true, save: async () => false }); await extension.execute('build'); assert.equal(calls.length, 0); });
test('saves dirty document before checking', async () => { let saved = false; Object.assign(vscode.window.activeTextEditor.document, { isDirty: true, save: async () => (saved = true) }); await extension.execute('check'); assert.ok(saved); assert.equal(calls[0].execution.args[0], 'check'); });
test('resolves relative compiler for the owning workspace', () => { folder = { uri: { fsPath: '/tmp/project two' } }; config = './bin/mylang'; assert.equal(extension.compilerPath(document()), '/tmp/project two/bin/mylang'); });
test('preserves absolute compiler path with spaces', () => { config = '/tmp/my compiler/mylang'; assert.equal(extension.compilerPath(document()), config); });
test('discovers compiler in repository workspace', () => { folder = { uri: { fsPath: path.resolve(__dirname, '../..') } }; assert.equal(extension.compilerPath(document()), defaultCompiler(path.join(folder.uri.fsPath, 'mylang'))); });
test('falls back to PATH', () => assert.equal(extension.compilerPath(document()), defaultCompiler('mylang')));
test('grammar matches actual keywords, numbers, and escapes', () => { const r = require('../syntaxes/vibes.tmLanguage.json').repository; assert.ok(new RegExp(r.keywords.patterns[0].match).test('if x:')); assert.ok(new RegExp(r.numbers.match).test('12.5')); assert.ok(new RegExp(r.strings.patterns[0].match).test('\\n')); });
test('indents colon blocks with comments', () => { const c = require('../language-configuration.json'); assert.ok(new RegExp(c.indentationRules.increaseIndentPattern).test('if x: # comment')); });

test('discovers current compiler when a source file is opened alone', () => {
  const doc = document();
  doc.uri.fsPath = path.resolve(__dirname, '../../examples/hello.vibes');
  assert.equal(extension.compilerPath(doc), defaultCompiler(path.resolve(__dirname, '../../mylang')));
});
test('discovers current compiler when only examples is the workspace', () => {
  const doc = document();
  doc.uri.fsPath = path.resolve(__dirname, '../../examples/hello.vibes');
  folder = { uri: { fsPath: path.dirname(doc.uri.fsPath) } };
  assert.equal(extension.compilerPath(doc), defaultCompiler(path.resolve(__dirname, '../../mylang')));
});
