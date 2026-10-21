/* Turns the contract's parse tree / AST into a uniform "view tree" that the
 * layout and renderer work with, plus lookup tables (parent, depth, order).
 *
 * The view tree is only a re-labelling of compiler output: same nodes, same
 * hierarchy, same token spans. No node is added, removed or reordered.
 */
(function (root) {
  'use strict';

  var contract = (root.PTAGEN && root.PTAGEN.contract) ||
    (typeof require === 'function' ? require('./contract.js') : null);

  var PUNCTUATION = { ASSIGN: true, SEMICOLON: true, LPAREN: true, RPAREN: true };
  var OPERATORS = { PLUS: true, MINUS: true, MUL: true, DIV: true };
  var TOKEN_GLYPHS = { ASSIGN: '=', SEMICOLON: ';', LPAREN: '(', RPAREN: ')', PLUS: '+',
    MINUS: '-', MUL: '*', DIV: '/' };

  // Visual class of a parse-tree terminal, by token type.
  function terminalClass(type) {
    if (PUNCTUATION[type]) return 'punct';
    if (OPERATORS[type]) return 'operator';
    return 'leaf';  // ID, NUMBER
  }

  // Visual class of an AST node, by kind.
  function astClass(kind) {
    if (kind === 'Program' || kind === 'Assignment') return 'structure';
    if (kind === 'Identifier' || kind === 'Number') return 'leaf';
    return 'operation';
  }

  function parseViewNode(n) {
    if (n.symbol === 'terminal') {
      var t = n.token;
      var plain = t.type === 'ID' || t.type === 'NUMBER';
      return {
        id: n.id, tree: 'parse', role: 'terminal', cls: 'terminal-' + terminalClass(t.type),
        title: plain ? t.lexeme : (TOKEN_GLYPHS[t.type] || t.lexeme),
        caption: t.type, first: n.firstToken, end: n.endToken, raw: n, children: []
      };
    }
    return {
      id: n.id, tree: 'parse', role: 'nonterminal', cls: 'nonterminal',
      title: n.symbol, caption: '', first: n.firstToken, end: n.endToken, raw: n, children: []
    };
  }

  function astViewNode(n) {
    var leaf = n.kind === 'Identifier' || n.kind === 'Number';
    return {
      id: n.id, tree: 'ast', role: leaf ? 'leaf' : 'operation', cls: 'ast-' + astClass(n.kind),
      title: leaf ? n.value : n.kind,
      caption: leaf ? n.kind : (contract.AST_GLYPHS[n.kind] || ''),
      first: n.firstToken, end: n.endToken, raw: n, children: []
    };
  }

  // Builds the view tree iteratively (no recursion on tree depth) and returns
  //   { root, byId: Map, parent: Map(id -> parent id|null), depth: Map, order: [views], count }
  function buildViewTree(rawRoot, treeKind) {
    var make = treeKind === 'ast' ? astViewNode : parseViewNode;
    var byId = new Map();
    var parent = new Map();
    var depth = new Map();
    var order = [];
    var rootView = make(rawRoot);
    byId.set(rootView.id, rootView);
    parent.set(rootView.id, null);
    depth.set(rootView.id, 0);

    var stack = [rootView];
    while (stack.length) {
      var view = stack.pop();
      order.push(view);
      var kids = view.raw.children;
      for (var i = 0; i < kids.length; i++) {
        var child = make(kids[i]);
        view.children.push(child);
        byId.set(child.id, child);
        parent.set(child.id, view.id);
        depth.set(child.id, depth.get(view.id) + 1);
      }
      for (var k = view.children.length - 1; k >= 0; k--) stack.push(view.children[k]);
    }
    return { root: rootView, byId: byId, parent: parent, depth: depth, order: order,
      count: order.length, kind: treeKind };
  }

  // Ids of every node strictly below `id`.
  function descendantIds(model, id) {
    var out = [];
    var stack = model.byId.get(id).children.slice();
    while (stack.length) {
      var v = stack.pop();
      out.push(v.id);
      for (var i = 0; i < v.children.length; i++) stack.push(v.children[i]);
    }
    return out;
  }

  // Ids of every ancestor of `id`, nearest first.
  function ancestorIds(model, id) {
    var out = [];
    var p = model.parent.get(id);
    while (p) { out.push(p); p = model.parent.get(p); }
    return out;
  }

  // Which nodes to start collapsed so a large tree opens at a readable size.
  // Small trees open fully expanded. For large ones, collapse nodes at the
  // first depth where the visible node count would exceed `budget`.
  function defaultCollapsed(model, budget) {
    var limit = budget || 220;
    var collapsed = new Set();
    if (model.count <= limit) return collapsed;

    var perDepth = [];
    model.order.forEach(function (v) {
      var d = model.depth.get(v.id);
      perDepth[d] = (perDepth[d] || 0) + 1;
    });
    var visible = 0;
    var cutoff = perDepth.length;  // depth whose nodes get collapsed
    for (var d = 0; d < perDepth.length; d++) {
      if (visible + perDepth[d] > limit && d >= 2) { cutoff = d - 1; break; }
      visible += perDepth[d];
    }
    model.order.forEach(function (v) {
      if (model.depth.get(v.id) === cutoff && v.children.length) collapsed.add(v.id);
    });
    return collapsed;
  }

  var api = {
    buildViewTree: buildViewTree,
    descendantIds: descendantIds,
    ancestorIds: ancestorIds,
    defaultCollapsed: defaultCollapsed,
    terminalClass: terminalClass,
    astClass: astClass
  };

  root.PTAGEN = root.PTAGEN || {};
  root.PTAGEN.treemodel = api;
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
})(typeof window !== 'undefined' ? window : globalThis);
