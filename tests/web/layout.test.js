'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const h = require('./helpers.js');

const never = () => false;

function modelOf(id, tree) {
  const r = h.response(id);
  return h.treemodel.buildViewTree(tree === 'ast' ? r.ast : r.parseTree, tree);
}

// No two boxes in the layout may overlap (touching is not allowed either:
// siblings are separated by the gap).
function assertNoOverlap(result) {
  const byRow = new Map();
  result.nodes.forEach((n) => {
    if (!byRow.has(n.depth)) byRow.set(n.depth, []);
    byRow.get(n.depth).push(n);
  });
  byRow.forEach((row) => {
    row.sort((a, b) => a.x - b.x);
    for (let i = 1; i < row.length; i++) {
      assert.ok(row[i].x >= row[i - 1].x + row[i - 1].w + 1,
        `overlap on a row: ${row[i - 1].id} and ${row[i].id}`);
    }
  });
}

function assertParentsCentred(result) {
  const kids = new Map();
  result.edges.forEach((e) => {
    if (!kids.has(e.from)) kids.set(e.from, []);
    kids.get(e.from).push(e.to);
  });
  kids.forEach((list, parent) => {
    const p = result.byId.get(parent);
    const first = result.byId.get(list[0]);
    const last = result.byId.get(list[list.length - 1]);
    assert.ok(Math.abs(p.cx - (first.cx + last.cx) / 2) < 1e-6, 'parent centred: ' + parent);
    list.forEach((c) => {
      assert.ok(result.byId.get(c).y > p.y + p.h, 'child below parent');
    });
  });
}

function assertInsideCanvas(result) {
  result.nodes.forEach((n) => {
    assert.ok(n.x >= 0 && n.y >= 0, 'non-negative coordinates: ' + n.id);
    assert.ok(n.x + n.w <= result.width + 1e-6, 'inside width: ' + n.id);
    assert.ok(n.y + n.h <= result.height + 1e-6, 'inside height: ' + n.id);
  });
}

test('single node layout', () => {
  const empty = modelOf('empty-program', 'ast');
  const res = h.layout.layoutTree(empty.root, h.measure, never);
  assert.equal(res.nodes.length, 1);
  assert.equal(res.edges.length, 0);
  assertInsideCanvas(res);
});

test('parse tree and AST of every accepted example lay out cleanly', () => {
  for (const id of ['precedence', 'parentheses', 'left-associativity', 'multiple-statements',
    'mixed-operators', 'nested-parentheses', 'empty-program']) {
    for (const tree of ['parse', 'ast']) {
      const m = modelOf(id, tree);
      const res = h.layout.layoutTree(m.root, h.measure, never);
      assert.equal(res.nodes.length, m.count, `${id}/${tree}: every node placed`);
      assert.equal(res.edges.length, m.count - 1, `${id}/${tree}: one edge per non-root`);
      assertNoOverlap(res);
      assertParentsCentred(res);
      assertInsideCanvas(res);
    }
  }
});

test('children keep their left-to-right order', () => {
  const m = modelOf('precedence', 'parse');
  const res = h.layout.layoutTree(m.root, h.measure, never);
  m.order.forEach((v) => {
    for (let i = 1; i < v.children.length; i++) {
      assert.ok(res.byId.get(v.children[i].id).cx > res.byId.get(v.children[i - 1].id).cx,
        'sibling order preserved under ' + v.id);
    }
  });
});

test('wider labels get wider boxes and never overlap neighbours', () => {
  const longName = {
    id: 'root', title: 'x', caption: '', children: [
      { id: 'a', title: 'a_very_long_identifier_name', caption: 'Identifier', children: [] },
      { id: 'b', title: 'b', caption: 'Identifier', children: [] }
    ]
  };
  const res = h.layout.layoutTree(longName, h.measure, never);
  assert.ok(res.byId.get('a').w > res.byId.get('b').w);
  assertNoOverlap(res);
});

test('collapsed nodes hide descendants and report how many', () => {
  const m = modelOf('precedence', 'ast');
  const add = m.order.find((v) => v.raw.kind === 'Add');
  const full = h.layout.layoutTree(m.root, h.measure, never);
  const collapsed = h.layout.layoutTree(m.root, h.measure, (id) => id === add.id);
  const node = collapsed.byId.get(add.id);
  assert.equal(node.collapsed, true);
  assert.equal(node.hiddenCount, 4);              // b, Multiply, c, 5
  assert.equal(collapsed.nodes.length, full.nodes.length - 4);
  assert.equal(collapsed.byId.has(add.children[0].id), false);
  assert.ok(collapsed.width <= full.width);
});

test('a 998-operand chain (1000-level AST) lays out without recursion errors', () => {
  const m = modelOf('stress-ast-at-limit', 'ast');
  assert.equal(m.count, 1998);
  const t0 = process.hrtime.bigint();
  const res = h.layout.layoutTree(m.root, h.measure, never);
  const ms = Number(process.hrtime.bigint() - t0) / 1e6;
  assert.equal(res.nodes.length, 1998);
  assertNoOverlap(res);
  assertParentsCentred(res);
  assert.ok(ms < 1000, 'layout took ' + ms.toFixed(1) + ' ms');
});

test('the 256-deep parenthesis parse tree lays out', () => {
  const m = modelOf('stress-nesting-at-limit', 'parse');
  const res = h.layout.layoutTree(m.root, h.measure, never);
  assert.equal(res.nodes.length, m.count);
  assertNoOverlap(res);
  assertInsideCanvas(res);
});

test('default collapsing keeps large trees to a readable size', () => {
  const small = modelOf('precedence', 'parse');
  assert.equal(h.treemodel.defaultCollapsed(small).size, 0);

  const big = modelOf('stress-ast-at-limit', 'ast');
  const set = h.treemodel.defaultCollapsed(big);
  assert.ok(set.size > 0);
  const res = h.layout.layoutTree(big.root, h.measure, (id) => set.has(id));
  assert.ok(res.nodes.length <= 260, 'visible nodes: ' + res.nodes.length);
  assert.ok(res.nodes.length >= 20);
});
