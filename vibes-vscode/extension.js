const vscode = require('vscode');
const fs = require('node:fs');
const path = require('node:path');
const toolchain = require('./toolchain');
const project = require('./project');
let output;
function showReport(report) {
  output ||= vscode.window.createOutputChannel('MyLang Setup');
  output.clear();
  output.appendLine(`Compiler: ${report.compiler}`);
  output.appendLine(report.details.join('\n'));
  output.appendLine(report.message);
  output.show(true);
}

function compilerPath(document) {
  const folder = vscode.workspace.getWorkspaceFolder(document.uri);
  const configured = vscode.workspace.getConfiguration('vibes', document.uri).get('compilerPath', '').trim();
  if (configured) {
    if (path.isAbsolute(configured) || !/[\\/]/.test(configured)) return configured;
    return path.resolve(folder ? folder.uri.fsPath : path.dirname(document.uri.fsPath), configured);
  }
  const bundled = path.join(__dirname, 'bin', 'mylang');
  if (fs.existsSync(bundled)) return bundled;
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
  if (command === 'run' || command === 'build') {
    const report = await toolchain.inspect(compiler, { source: document.uri.fsPath });
    if (!report.ok) {
      showReport(report);
      await vscode.window.showErrorMessage(report.message);
      return;
    }
  }
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

async function checkSetup() {
  if (!vscode.workspace.isTrusted) return vscode.window.showWarningMessage('Trust this workspace before checking executable dependencies.');
  if (process.platform === 'win32') return vscode.window.showErrorMessage('Open a WSL workspace and install MyLang there. Native Windows is not supported.');
  const choice = await vscode.window.showQuickPick(['Programs (Clang)', 'Games (Clang + Raylib)'], { title: 'Check MyLang setup' });
  if (!choice) return;
  const document = vscode.window.activeTextEditor?.document || { uri: vscode.workspace.workspaceFolders?.[0]?.uri || vscode.Uri.file(process.cwd()) };
  const report = await toolchain.inspect(compilerPath(document), { game: choice.startsWith('Games') });
  showReport(report);
  if (report.ok) await vscode.window.showInformationMessage(report.message);
  else await vscode.window.showErrorMessage(report.message);
}

async function createProject() {
  const kind = await vscode.window.showQuickPick([
    { label: 'Hello World', description: 'A simple program; requires Clang', kind: 'program' },
    { label: 'Movable Ball', description: 'An arrow-key game; requires Clang and Raylib', kind: 'game' }
  ], { title: 'Create a MyLang project' });
  if (!kind) return;
  const parents = await vscode.window.showOpenDialog({ canSelectFiles: false, canSelectFolders: true, canSelectMany: false, openLabel: 'Choose parent folder' });
  if (!parents?.length) return;
  if (parents[0].scheme !== 'file') return vscode.window.showErrorMessage('Choose a local folder (or a folder on your remote extension host).');
  const name = await vscode.window.showInputBox({ title: 'Project folder name', value: 'hello-vibes', validateInput: value => /^[a-zA-Z0-9][a-zA-Z0-9_-]{0,63}$/.test(value) ? undefined : 'Use 1–64 letters, numbers, underscores, or hyphens.' });
  if (!name) return;
  try {
    const directory = await project.createProject(parents[0].fsPath, name, kind.kind);
    const action = await vscode.window.showInformationMessage('Project created. Open its folder, then open main.vibes and click Run.', 'Open Project');
    if (action === 'Open Project') await vscode.commands.executeCommand('vscode.openFolder', vscode.Uri.file(directory), { forceNewWindow: true });
  } catch (error) {
    await vscode.window.showErrorMessage(error.code === 'EEXIST' ? 'That folder already exists. Choose a new project name.' : `Could not create project: ${error.message}`);
  }
}

function activate(context) {
  context.subscriptions.push(vscode.commands.registerCommand('vibes.checkSetup', checkSetup));
  context.subscriptions.push(vscode.commands.registerCommand('vibes.createProject', createProject));
  context.subscriptions.push({ dispose() { output?.dispose(); output = undefined; } });
  for (const [id, command] of Object.entries({ runFile: 'run', buildFile: 'build', checkFile: 'check', emitFile: 'emit' })) {
    context.subscriptions.push(vscode.commands.registerCommand(`vibes.${id}`, () => execute(command)));
  }
}

module.exports = { activate, compilerPath, execute };
