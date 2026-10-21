'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { sourcemap, response } = require('./helpers.js');

test('line starts follow LF, CRLF and lone CR', () => {
  assert.deepEqual(sourcemap.lineStarts('a\nb\r\nc\rd'), [0, 2, 5, 7]);
  assert.deepEqual(sourcemap.lineStarts(''), [0]);
  assert.equal(sourcemap.normalizeNewlines('a\r\nb\rc\nd'), 'a\nb\nc\nd');
});

test('token positions map to offsets', () => {
  const src = 'x = 1;\n  y = 2;';
  const starts = sourcemap.lineStarts(src);
  assert.equal(sourcemap.positionToOffset(src, starts, 1, 1), 0);
  assert.equal(sourcemap.positionToOffset(src, starts, 2, 3), 9);
  assert.equal(src[9], 'y');
  assert.equal(sourcemap.positionToOffset(src, starts, 3, 1), -1);
});

test('each real token maps back to exactly its lexeme', () => {
  for (const id of ['precedence', 'parentheses', 'multiple-statements', 'mixed-operators']) {
    const r = response(id);
    const src = r.source;
    const starts = sourcemap.lineStarts(src);
    r.tokens.filter((t) => t.type !== 'END').forEach((t) => {
      const range = sourcemap.tokenRange(src, starts, t);
      assert.ok(range, id + ' token ' + t.index);
      assert.equal(src.slice(range.start, range.end), t.lexeme);
    });
  }
});

test('a token span covers from its first to its last token', () => {
  const r = response('precedence');         // a = b + c * 5;
  const starts = sourcemap.lineStarts(r.source);
  const range = sourcemap.spanRange(r.source, starts, r.tokens, 2, 7);
  assert.equal(r.source.slice(range.start, range.end), 'b + c * 5');
  const loc = sourcemap.spanLocation(r.source, starts, r.tokens, 2, 7);
  assert.deepEqual([loc.startLine, loc.startColumn, loc.endLine, loc.endColumn], [1, 5, 1, 13]);
  assert.equal(loc.text, 'b + c * 5');
});

test('end of the span is exclusive', () => {
  const r = response('precedence');
  const starts = sourcemap.lineStarts(r.source);
  const one = sourcemap.spanRange(r.source, starts, r.tokens, 2, 3);
  assert.equal(r.source.slice(one.start, one.end), 'b');
});

test('an operation node covering parentheses includes them', () => {
  const r = response('parentheses');          // result = (x + y) * z;
  const mult = r.ast.children[0].children[1];
  assert.equal(mult.kind, 'Multiply');
  const starts = sourcemap.lineStarts(r.source);
  const range = sourcemap.spanRange(r.source, starts, r.tokens, mult.firstToken, mult.endToken);
  assert.equal(r.source.slice(range.start, range.end), '(x + y) * z');
  const add = mult.children[0];
  const inner = sourcemap.spanRange(r.source, starts, r.tokens, add.firstToken, add.endToken);
  assert.equal(r.source.slice(inner.start, inner.end), 'x + y');
});

test('an empty span has no source range', () => {
  const r = response('empty-program');
  const starts = sourcemap.lineStarts(r.source);
  assert.equal(sourcemap.spanRange(r.source, starts, r.tokens, 0, 0), null);
  assert.equal(sourcemap.spanLocation(r.source, starts, r.tokens, 0, 0), null);
});

test('multi-line spans use each token own line and column', () => {
  const r = response('multiple-statements');
  const starts = sourcemap.lineStarts(r.source);
  const prog = r.ast;
  const loc = sourcemap.spanLocation(r.source, starts, r.tokens, prog.firstToken, prog.endToken);
  assert.equal(loc.startLine, 1);
  assert.equal(loc.endLine, 3);
});

test('diagnostic ranges: offending text, or a zero-width point at end of input', () => {
  const star = response('error-unexpected-operator').diagnostics[0];
  const src = response('error-unexpected-operator').source;
  const starts = sourcemap.lineStarts(src);
  const range = sourcemap.diagnosticRange(src, starts, star);
  assert.equal(src.slice(range.start, range.end), '*');

  const eof = { line: 1, column: 7, found: '' };
  const point = sourcemap.diagnosticRange('a = (b', sourcemap.lineStarts('a = (b'), eof);
  assert.equal(point.start, point.end);
  assert.equal(point.start, 6);
});

test('columns count code points, not UTF-16 units', () => {
  const src = '\u{1F600} = 1;';               // an astral character before the tokens
  const starts = sourcemap.lineStarts(src);
  assert.equal(sourcemap.positionToOffset(src, starts, 1, 3), 3);   // after emoji and space
  assert.equal(sourcemap.codePointLength('\u{1F600}'), 1);
});

test('token index lookup by position', () => {
  const r = response('precedence');
  assert.equal(sourcemap.tokenIndexAt(r.tokens, 1, 7), 3);
  assert.equal(sourcemap.tokenIndexAt(r.tokens, 9, 9), -1);
});
