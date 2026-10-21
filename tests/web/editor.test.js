'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const { editor } = require('./helpers.js');

test('plain text is escaped and unchanged apart from the trailing newline', () => {
  assert.equal(editor.buildBackdropHtml('a < b & c', []), 'a &lt; b &amp; c\n');
});

test('source text that looks like HTML is escaped, never interpreted', () => {
  const evil = '<img src=x onerror=alert(1)> </mark><script>x</script> "q" \'s\' &amp;';
  const html = editor.buildBackdropHtml(evil, [{ start: 2, end: 10, cls: 'error' }]);
  assert.ok(!/<img|<script/i.test(html));
  // Every '<' left in the output belongs to our own mark tags.
  const stripped = html.replace(/<mark class="[^"]*">|<\/mark>|<span class="[^"]*"><\/span>/g, '');
  assert.ok(!stripped.includes('<'));
  // And the visible text is exactly the source.
  const text = html.replace(/<[^>]+>/g, '').replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&amp;/g, '&');
  assert.equal(text, evil + '\n');
});

test('ranges become marks with their classes', () => {
  const html = editor.buildBackdropHtml('a = b;', [{ start: 4, end: 5, cls: 'node' }]);
  assert.equal(html, 'a = <mark class="mk-node">b</mark>;\n');
});

test('overlapping ranges combine classes', () => {
  const html = editor.buildBackdropHtml('abcdef', [
    { start: 1, end: 5, cls: 'node' },
    { start: 3, end: 6, cls: 'error' }
  ]);
  assert.equal(html,
    'a<mark class="mk-node">bc</mark><mark class="mk-node mk-error">de</mark>' +
    '<mark class="mk-error">f</mark>\n');
});

test('a zero-width range becomes a point marker at its offset', () => {
  const html = editor.buildBackdropHtml('a = (b', [{ start: 6, end: 6, cls: 'error', point: true }]);
  assert.equal(html, 'a = (b<span class="mk-point mk-error"></span>\n');
});

test('marked text keeps every character, in order', () => {
  const text = 'x = (1 + 2) * y;\nz = 3;';
  const html = editor.buildBackdropHtml(text, [
    { start: 4, end: 11, cls: 'node' }, { start: 17, end: 18, cls: 'error' }
  ]);
  const plain = html.replace(/<[^>]+>/g, '').replace(/&lt;/g, '<').replace(/&amp;/g, '&');
  assert.equal(plain, text + '\n');
});
