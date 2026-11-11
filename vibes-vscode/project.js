const fs = require('node:fs/promises');
const path = require('node:path');
const templates = {
  program: 'fn add(a, b):\n    return a + b\n\nprint "Hello, Vibes!"\nprint "20 + 22 = ", add(20, 22)\n',
  game: 'window "My First Game", 800, 600\n\nx = 400\ny = 300\n\nif key_right:\n    x = x + 5\nif key_left:\n    x = x - 5\nif key_up:\n    y = y - 5\nif key_down:\n    y = y + 5\n\ncircle x, y, 30, "red"\n'
};
async function createProject(parent, name, kind) {
  if (!/^[a-zA-Z0-9][a-zA-Z0-9_-]{0,63}$/.test(name)) throw new Error('Use 1–64 letters, numbers, underscores, or hyphens.');
  if (!templates[kind]) throw new Error('Unknown project template.');
  const directory = path.join(parent, name);
  // Exclusive directory creation protects every existing project, even empty ones.
  await fs.mkdir(directory);
  await fs.mkdir(path.join(directory, '.vscode'));
  await fs.writeFile(path.join(directory, 'main.vibes'), templates[kind], { flag: 'wx' });
  await fs.writeFile(path.join(directory, '.vscode', 'extensions.json'), JSON.stringify({ recommendations: ['mylang.vibes-vscode'] }, null, 2));
  await fs.writeFile(path.join(directory, '.gitignore'), '/main\n/main.c\n');
  await fs.writeFile(path.join(directory, 'README.md'), `# ${name}\n\nOpen main.vibes and click Run in VS Code.\nUse MyLang: Check Setup if tools are missing.\n${kind === 'game' ? '\nMove the ball with the arrow keys. This game requires Raylib.\n' : ''}`);
  return directory;
}
module.exports = { createProject, templates };
