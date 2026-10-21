/* Tree layout: pure geometry, no DOM.
 *
 * Algorithm - "extent packing". Every subtree is described by how far it
 * reaches left (l) and right (r) of its root's centre:
 *
 *   - children are placed left to right so that neighbouring subtrees are
 *     exactly `hGap` apart (right extent of one + gap + left extent of next);
 *   - a parent is centred over the span from its first child's centre to its
 *     last child's centre;
 *   - the parent's own extents are the larger of its box half-width and the
 *     reach of the children block.
 *
 * Depth maps to a fixed row pitch (row height = tallest node on that depth).
 * Because sibling subtrees are separated by their full extents, no two
 * subtrees can overlap, labels get exactly the width they need, and unary
 * chains stay vertical. It is O(n) and iterative (no recursion on tree
 * depth), so a 1000-level AST lays out without stack concerns.
 *
 * Collapsed nodes lay out as leaves and report how many nodes they hide.
 */
(function (root) {
  'use strict';

  var DEFAULTS = { hGap: 18, vGap: 34, margin: 24 };

  // measure(viewNode) -> { w, h };  isCollapsed(id) -> boolean
  function layoutTree(rootView, measure, isCollapsed, options) {
    var opt = Object.assign({}, DEFAULTS, options || {});
    var hGap = opt.hGap, vGap = opt.vGap, margin = opt.margin;

    var size = new Map();     // id -> {w, h}
    var ext = new Map();      // id -> {l, r}
    var shown = new Map();    // id -> visible children
    var dx = new Map();       // id -> child centre minus parent centre
    var hidden = new Map();   // id -> hidden descendant count (collapsed nodes)

    // Discover visible nodes (pre-order, iterative).
    var order = [];
    var stack = [rootView];
    while (stack.length) {
      var v = stack.pop();
      order.push(v);
      var kids = (v.children.length && !isCollapsed(v.id)) ? v.children : [];
      shown.set(v.id, kids);
      for (var i = 0; i < kids.length; i++) stack.push(kids[i]);
    }

    // Pass 1, children before parents: extents and child offsets.
    for (var p = order.length - 1; p >= 0; p--) {
      var node = order[p];
      var m = measure(node);
      size.set(node.id, m);
      var visibleKids = shown.get(node.id);

      if (!visibleKids.length) {
        ext.set(node.id, { l: m.w / 2, r: m.w / 2 });
        if (node.children.length) hidden.set(node.id, countDescendants(node));
        continue;
      }

      var pos = [];
      var cursor = 0;
      for (var k = 0; k < visibleKids.length; k++) {
        var e = ext.get(visibleKids[k].id);
        if (k === 0) cursor = e.l;
        else cursor = pos[k - 1] + ext.get(visibleKids[k - 1].id).r + hGap + e.l;
        pos.push(cursor);
      }
      var lastE = ext.get(visibleKids[visibleKids.length - 1].id);
      var blockRight = pos[pos.length - 1] + lastE.r;
      var mid = (pos[0] + pos[pos.length - 1]) / 2;

      for (var q = 0; q < visibleKids.length; q++) dx.set(visibleKids[q].id, pos[q] - mid);
      ext.set(node.id, {
        l: Math.max(m.w / 2, mid),
        r: Math.max(m.w / 2, blockRight - mid)
      });
    }

    // Pass 2: absolute centres from the root downwards.
    var cx = new Map();
    var depthOf = new Map();
    cx.set(rootView.id, margin + ext.get(rootView.id).l);
    depthOf.set(rootView.id, 0);
    var maxDepth = 0;
    var rowHeight = [];
    order.forEach(function (n) {   // pre-order guarantees parents come first
      var d = depthOf.get(n.id);
      if (d > maxDepth) maxDepth = d;
      var h = size.get(n.id).h;
      if ((rowHeight[d] || 0) < h) rowHeight[d] = h;
      shown.get(n.id).forEach(function (child) {
        cx.set(child.id, cx.get(n.id) + dx.get(child.id));
        depthOf.set(child.id, d + 1);
      });
    });

    var rowTop = [margin];
    for (var r = 1; r <= maxDepth; r++) rowTop[r] = rowTop[r - 1] + rowHeight[r - 1] + vGap;

    var nodes = [];
    var byId = new Map();
    var edges = [];
    order.forEach(function (n) {
      var d = size.get(n.id);
      var entry = {
        id: n.id,
        view: n,
        depth: depthOf.get(n.id),
        x: cx.get(n.id) - d.w / 2,
        y: rowTop[depthOf.get(n.id)],
        w: d.w,
        h: d.h,
        cx: cx.get(n.id),
        childCount: n.children.length,
        collapsed: n.children.length > 0 && shown.get(n.id).length === 0,
        hiddenCount: hidden.get(n.id) || 0
      };
      nodes.push(entry);
      byId.set(n.id, entry);
      shown.get(n.id).forEach(function (child) { edges.push({ from: n.id, to: child.id }); });
    });

    var rootExt = ext.get(rootView.id);
    return {
      nodes: nodes,
      byId: byId,
      edges: edges,
      width: rootExt.l + rootExt.r + 2 * margin,
      height: rowTop[maxDepth] + rowHeight[maxDepth] + margin,
      rowTop: rowTop
    };
  }

  function countDescendants(view) {
    var n = 0;
    var stack = view.children.slice();
    while (stack.length) {
      var v = stack.pop();
      n++;
      for (var i = 0; i < v.children.length; i++) stack.push(v.children[i]);
    }
    return n;
  }

  var api = { layoutTree: layoutTree, DEFAULTS: DEFAULTS };

  root.PTAGEN = root.PTAGEN || {};
  root.PTAGEN.layout = api;
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
})(typeof window !== 'undefined' ? window : globalThis);
