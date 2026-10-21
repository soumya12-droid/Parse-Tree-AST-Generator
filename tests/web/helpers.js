'use strict';
// Shared helpers for the frontend tests (Node's built-in test runner).
// Loads the real generated fixtures, which are captured output of the C++ pipeline.

const path = require('node:path');

const webDir = path.join(__dirname, '..', '..', 'web');

globalThis.window = globalThis;           // fixtures files assign to window.*
require(path.join(webDir, 'fixtures', 'fixtures.js'));
require(path.join(webDir, 'fixtures', 'fixtures-stress.js'));

const contract = require(path.join(webDir, 'js', 'contract.js'));
const sourcemap = require(path.join(webDir, 'js', 'sourcemap.js'));
const treemodel = require(path.join(webDir, 'js', 'treemodel.js'));
const layout = require(path.join(webDir, 'js', 'layout.js'));
const analyzer = require(path.join(webDir, 'js', 'analyzer.js'));
const state = require(path.join(webDir, 'js', 'state.js'));
const editor = require(path.join(webDir, 'js', 'editor.js'));

const fixtures = globalThis.PTAGEN_FIXTURES;
const stress = globalThis.PTAGEN_STRESS_RESPONSES;

function fixture(id) {
  const f = fixtures.find((x) => x.id === id);
  if (!f) throw new Error('no fixture ' + id);
  return f;
}

// Response for a fixture id, from the inline list or the lazy stress file.
function response(id) {
  const f = fixture(id);
  return f.response || stress[id];
}

// Nested text form of a contract AST: Add(Identifier(b), Multiply(...)).
function astShape(node) {
  if (node.kind === 'Identifier' || node.kind === 'Number') {
    return node.kind + '(' + node.value + ')';
  }
  return node.kind + '(' + node.children.map(astShape).join(', ') + ')';
}

// Nested text form of a contract parse tree.
function parseShape(node) {
  if (node.symbol === 'terminal') return node.token.type + '(' + node.token.lexeme + ')';
  return '(' + node.symbol + (node.children.length ? ' ' : '') +
    node.children.map(parseShape).join(' ') + ')';
}

// A state with results loaded for fixture `id`.
function loadedState(id) {
  const s0 = state.initialState();
  const s1 = state.sourceChanged(s0, fixture(id).source);
  return state.withResults(s1, response(id), { kind: 'fixture', id, title: fixture(id).title });
}

// Fake text measurement: 8px per character, fixed height.
function measure(view) {
  const text = view.title.length > view.caption.length ? view.title : view.caption;
  return { w: Math.max(44, text.length * 8 + 26), h: view.caption ? 46 : 32 };
}

// Starts the real `ptagen serve` on a free port. Resolves { port, baseUrl, stop }
// where stop() closes the server's stdin (its clean-shutdown signal) and
// resolves { code, output } when the process exits.
function startServer(extraArgs) {
  const { spawn } = require('node:child_process');
  const exe = process.env.PTAGEN_EXE || path.join(__dirname, '..', '..', 'build', 'ptagen.exe');
  return new Promise((resolve, reject) => {
    const child = spawn(exe, ['serve', '--port', '0', '--stop-on-stdin-close', '--quiet']
      .concat(extraArgs || []), { stdio: ['pipe', 'pipe', 'pipe'] });
    let output = '';
    let ready = false;
    const exited = new Promise((done) => child.on('exit', (code) => done(code)));
    const timer = setTimeout(() => { child.kill(); reject(new Error('server did not start: ' + output)); }, 10000);
    child.stdout.on('data', (chunk) => {
      output += chunk;
      const m = /Listening on http:\/\/127\.0\.0\.1:(\d+)\//.exec(output);
      if (m && !ready) {
        ready = true;
        clearTimeout(timer);
        const port = Number(m[1]);
        resolve({
          port,
          baseUrl: 'http://127.0.0.1:' + port,
          stop: async () => {
            child.stdin.end();
            const code = await Promise.race([
              exited,
              new Promise((r) => setTimeout(() => { child.kill(); r('timeout'); }, 8000))
            ]);
            return { code, output };
          }
        });
      }
    });
    child.stderr.on('data', (chunk) => { output += chunk; });
    child.on('error', (err) => { clearTimeout(timer); reject(err); });
  });
}

module.exports = {
  startServer,
  contract, sourcemap, treemodel, layout, analyzer, state, editor,
  fixtures, stress, fixture, response, astShape, parseShape, loadedState, measure
};
