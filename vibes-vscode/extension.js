const vscode = require('vscode');
const fs = require('node:fs');
const path = require('node:path');

function compilerPath(document) {
  const folder = vscode.workspace.getWorkspaceFolder(document.uri);
  const configured = vscode.workspace.getConfiguration('vibes', document.uri).get('compilerPath', '').trim();
  if (configured) {
    if (path.isAbsolute(configured) || !/[\\/]/.test(configured)) return configured;
    return path.resolve(folder ? folder.uri.fsPath : path.dirname(document.uri.fsPath), configured);
  }
  if (folder) {
    const local = path.join(folder.uri.fsPath, 'mylang');
    try { fs.accessSync(local, fs.constants.X_OK); return local; } catch (_) { /* Try PATH. */ }
  }
  // A source file may be opened alone, or with examples/ as the workspace.
  // Recognize a compiler source checkout before falling back to an older PATH install.
  let directory = path.dirname(document.uri.fsPath);
  while (true) {
    const local = path.join(directory, 'mylang');
    if (fs.existsSync(path.join(directory, 'src', 'main.c')) &&
        fs.existsSync(path.join(directory, 'Makefile')) &&
        fs.existsSync(path.join(directory, 'VERSION'))) {
      try { fs.accessSync(local, fs.constants.X_OK); return local; } catch (_) { /* Try parent. */ }
    }
    const parent = path.dirname(directory);
    if (parent === directory) break;
    directory = parent;
  }
  return 'mylang';
}

async function execute(command) {
  if (!vscode.workspace.isTrusted) {
    return vscode.window.showWarningMessage('Trust this workspace before running the MyLang compiler.');
  }
  if (process.platform === 'win32') {
    return vscode.window.showErrorMessage('MyLang requires macOS or Linux. On Windows, reopen this folder in WSL and install the extension there.');
  }
  const document = vscode.window.activeTextEditor?.document;
  if (!document || document.languageId !== 'vibes') {
    return vscode.window.showWarningMessage('Open a .vibes file first.');
  }
  if (document.isUntitled || document.uri.scheme !== 'file') {
    return vscode.window.showWarningMessage('Save this .vibes file to disk first.');
  }
  if (document.isDirty && !(await document.save())) return;
  const folder = vscode.workspace.getWorkspaceFolder(document.uri);
  const compiler = compilerPath(document);
  // ProcessExecution passes arguments directly: filenames never become shell code.
  const execution = new vscode.ProcessExecution(compiler, [command, document.uri.fsPath], {
    cwd: path.dirname(document.uri.fsPath)
  });
  const task = new vscode.Task(
    { type: 'vibes', command, file: document.uri.fsPath },
    folder || vscode.TaskScope.Global,
    `${command}: ${path.basename(document.uri.fsPath)}`, 'MyLang', execution, []
  );
  task.presentationOptions = { reveal: vscode.TaskRevealKind.Always, panel: vscode.TaskPanelKind.Dedicated, clear: true };
  try {
    await vscode.tasks.executeTask(task);
  } catch (error) {
    vscode.window.showErrorMessage(`Cannot start MyLang (${compiler}). Build it with make or set vibes.compilerPath. ${error.message}`);
  }
}

function activate(context) {
  for (const [id, command] of Object.entries({ runFile: 'run', buildFile: 'build', checkFile: 'check', emitFile: 'emit' })) {
    context.subscriptions.push(vscode.commands.registerCommand(`vibes.${id}`, () => execute(command)));
  }
}

module.exports = { activate, compilerPath, execute };
