/* SVG tree renderer with zoom, pan, selection, collapse and keyboard support.
 *
 * It draws exactly the layout it is given (layout.js) for the view tree it is
 * given (treemodel.js); it computes no tree structure of its own.
 *
 * Zoom works by sizing the <svg> (the viewBox stays fixed) inside a scrolling
 * container, so native scrollbars, drag-to-pan and wheel scrolling all keep
 * working and the layout geometry is never touched by zooming.
 */
(function (root) {
  'use strict';

  var SVG_NS = 'http://www.w3.org/2000/svg';
  var P = root.PTAGEN;
  var layoutApi = P && P.layout;

  // The lower bound is low on purpose: "Fit" must really show the whole tree,
  // even a 9000px-wide one (an overview, not a reading size; zoom in to read).
  var ZOOM_MIN = 0.03, ZOOM_MAX = 2.5, ZOOM_STEP = 1.2;
  var MAX_LABEL = 24;

  function el(name, attrs, text) {
    var node = document.createElementNS(SVG_NS, name);
    if (attrs) Object.keys(attrs).forEach(function (k) { node.setAttribute(k, attrs[k]); });
    if (text !== undefined) node.textContent = text;
    return node;
  }

  function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)); }

  // Text widths from the real fonts, via a canvas; falls back to monospace estimates.
  function createMeasurer() {
    var mono = 'monospace';
    try {
      var v = getComputedStyle(document.documentElement).getPropertyValue('--mono').trim();
      if (v) mono = v;
    } catch (e) { /* ignore */ }
    var ctx = null;
    try { ctx = document.createElement('canvas').getContext('2d'); } catch (e) { ctx = null; }
    function width(text, font, fallbackPerChar) {
      if (ctx) {
        ctx.font = font;
        var w = ctx.measureText(text).width;
        if (w > 0) return w;
      }
      return text.length * fallbackPerChar;
    }
    return {
      main: function (t) { return width(t, '600 13px ' + mono, 7.9); },
      caption: function (t) { return width(t, '500 10.5px ' + mono, 6.4); }
    };
  }

  function shortLabel(text) {
    return text.length > MAX_LABEL ? text.slice(0, MAX_LABEL - 1) + '…' : text;
  }

  function createTreeView(options) {
    var container = options.container;         // scrolling element
    var onSelect = options.onSelect || function () {};
    var onToggle = options.onToggle || function () {};
    var onZoom = options.onZoom || function () {};
    var onBackground = options.onBackground || function () {};
    var treeKind = options.kind;               // 'parse' | 'ast' (for aria labels)

    var measurer = createMeasurer();
    var zoom = 1;
    var current = null;        // { svg, layout, model, nodeEls, edgeEls, parentOf, childrenOf }
    var selectedId = null;

    function measure(view) {
      var title = shortLabel(view.title);
      var w = measurer.main(title);
      if (view.caption) w = Math.max(w, measurer.caption(view.caption));
      return { w: Math.max(44, Math.ceil(w + 26)), h: view.caption ? 46 : 32 };
    }

    function applyZoom() {
      if (!current) return;
      current.svg.setAttribute('width', String(Math.round(current.layout.width * zoom)));
      current.svg.setAttribute('height', String(Math.round(current.layout.height * zoom)));
      onZoom(zoom);
    }

    function clear() {
      container.textContent = '';
      current = null;
      selectedId = null;
    }

    function elbow(parent, child) {
      var x1 = parent.cx, y1 = parent.y + parent.h;
      var x2 = child.cx, y2 = child.y;
      var my = y1 + (y2 - y1) / 2;
      return 'M' + x1 + ' ' + y1 + 'V' + my + 'H' + x2 + 'V' + y2;
    }

    function nodeAria(view, node) {
      var what = view.role === 'terminal'
        ? 'token ' + view.caption + ' ' + view.title
        : view.title;
      var state = node.childCount
        ? (node.collapsed ? ', collapsed, ' + node.hiddenCount + ' nodes hidden' : ', expanded')
        : '';
      return (treeKind === 'ast' ? 'AST ' : 'Parse tree ') + what + state;
    }

    // Draws `model` with the given collapsed set. opts:
    //   fresh   - new analysis: choose an initial zoom and scroll to the root
    //   anchor  - id of a node to keep at the same screen position (toggle)
    function render(model, collapsedSet, selected, opts) {
      opts = opts || {};
      var anchorBefore = null;
      if (opts.anchor && current && current.layout.byId.has(opts.anchor)) {
        var an = current.layout.byId.get(opts.anchor);
        anchorBefore = {
          x: an.cx * zoom - container.scrollLeft,
          y: an.y * zoom - container.scrollTop
        };
      }

      var layout = layoutApi.layoutTree(model.root, measure,
        function (id) { return collapsedSet.has(id); });

      var svg = el('svg', {
        'class': 'tree-svg',
        viewBox: '0 0 ' + layout.width + ' ' + layout.height,
        role: 'tree',
        'aria-label': treeKind === 'ast' ? 'Abstract syntax tree' : 'Concrete parse tree'
      });
      var edgeLayer = el('g', { 'class': 'edges' });
      var nodeLayer = el('g', { 'class': 'nodes' });
      svg.appendChild(edgeLayer);
      svg.appendChild(nodeLayer);

      var edgeEls = new Map();
      var childrenOf = new Map();
      var parentOf = new Map();
      layout.edges.forEach(function (edge) {
        var p = layout.byId.get(edge.from), c = layout.byId.get(edge.to);
        var path = el('path', { 'class': 'edge', d: elbow(p, c) });
        edgeLayer.appendChild(path);
        edgeEls.set(edge.to, path);
        if (!childrenOf.has(edge.from)) childrenOf.set(edge.from, []);
        childrenOf.get(edge.from).push(edge.to);
        parentOf.set(edge.to, edge.from);
      });

      var nodeEls = new Map();
      layout.nodes.forEach(function (node) {
        var v = node.view;
        var g = el('g', {
          'class': 'node ' + v.cls,
          'data-id': node.id,
          tabindex: node.id === model.root.id ? '0' : '-1',
          role: 'treeitem',
          'aria-label': nodeAria(v, node)
        });
        g.appendChild(el('rect', { x: node.x, y: node.y, width: node.w, height: node.h, rx: 6 }));
        var label = shortLabel(v.title);
        if (v.caption) {
          g.appendChild(el('text', { 'class': 't-cap', x: node.cx, y: node.y + 15 }, v.caption));
          g.appendChild(el('text', { 'class': 't-main', x: node.cx, y: node.y + 34 }, label));
        } else {
          g.appendChild(el('text', { 'class': 't-main', x: node.cx, y: node.y + node.h / 2 + 4.5 },
            label));
        }
        if (label !== v.title) g.appendChild(el('title', null, v.title));

        if (node.childCount) {
          var tg = el('g', { 'class': 'toggle', 'data-toggle': node.id });
          var cy = node.y + node.h;
          tg.appendChild(el('circle', { cx: node.cx, cy: cy, r: 8 }));
          tg.appendChild(el('text', { x: node.cx, y: cy + 4 }, node.collapsed ? '+' : '−'));
          tg.appendChild(el('title', null, node.collapsed
            ? 'Expand (' + node.hiddenCount + ' hidden nodes)'
            : 'Collapse'));
          g.appendChild(tg);
          if (node.collapsed) {
            g.appendChild(el('text', { 'class': 't-hidden', x: node.cx + 14, y: cy + 4,
              'text-anchor': 'start' }, '+' + node.hiddenCount));
          }
        }
        nodeLayer.appendChild(g);
        nodeEls.set(node.id, g);
      });

      container.textContent = '';
      container.appendChild(svg);
      current = { svg: svg, layout: layout, model: model, nodeEls: nodeEls, edgeEls: edgeEls,
        parentOf: parentOf, childrenOf: childrenOf };

      if (opts.fresh) {
        var avail = container.clientWidth || layout.width;
        zoom = clamp(avail / layout.width, 0.6, 1);
      }
      applyZoom();

      if (opts.fresh) {
        container.scrollTop = 0;
        var rootNode = layout.byId.get(model.root.id);
        container.scrollLeft = Math.max(0, rootNode.cx * zoom - container.clientWidth / 2);
      } else if (anchorBefore && layout.byId.has(opts.anchor)) {
        var an2 = layout.byId.get(opts.anchor);
        container.scrollLeft = an2.cx * zoom - anchorBefore.x;
        container.scrollTop = an2.y * zoom - anchorBefore.y;
      }
      setSelected(selected, opts.selectedSubtree);
    }

    // Marks the selected node and (optionally) its subtree; does not re-layout.
    function setSelected(id, subtreeIds) {
      if (!current) return;
      if (selectedId && current.nodeEls.has(selectedId)) {
        current.nodeEls.get(selectedId).classList.remove('sel');
      }
      current.nodeEls.forEach(function (g) { g.classList.remove('in-sub'); });
      current.edgeEls.forEach(function (p) { p.classList.remove('hl'); });
      selectedId = id || null;
      if (!selectedId) return;
      var g = current.nodeEls.get(selectedId);
      if (g) g.classList.add('sel');
      (subtreeIds || []).forEach(function (sid) {
        var sg = current.nodeEls.get(sid);
        if (sg) sg.classList.add('in-sub');
        var edge = current.edgeEls.get(sid);
        if (edge) edge.classList.add('hl');
      });
    }

    function setZoom(next, anchor) {
      if (!current) return;
      var z = clamp(next, ZOOM_MIN, ZOOM_MAX);
      if (z === zoom) return;
      var cxView = anchor ? anchor.x : container.clientWidth / 2;
      var cyView = anchor ? anchor.y : container.clientHeight / 2;
      var contentX = (container.scrollLeft + cxView) / zoom;
      var contentY = (container.scrollTop + cyView) / zoom;
      zoom = z;
      applyZoom();
      container.scrollLeft = contentX * zoom - cxView;
      container.scrollTop = contentY * zoom - cyView;
    }

    // Scrolls just enough to bring the node (and its +/- control) fully into
    // view. It measures the node's actual on-screen box rather than computing
    // it from the layout model, so it stays correct whatever the zoom or
    // scrollbar state is (a model-based version left a node half outside the
    // view after Fit -> Reset on a very large tree).
    function scrollToNode(id) {
      if (!current || !current.nodeEls.has(id)) return;
      var pad = 24;
      var view = container.getBoundingClientRect();
      var box = current.nodeEls.get(id).getBoundingClientRect();
      var viewW = container.clientWidth, viewH = container.clientHeight;
      var left = box.left - view.left, right = box.right - view.left;
      var top = box.top - view.top, bottom = box.bottom - view.top;
      if (left < pad) container.scrollLeft += left - pad;
      else if (right > viewW - pad) container.scrollLeft += right - (viewW - pad);
      if (top < pad) container.scrollTop += top - pad;
      else if (bottom > viewH - pad) container.scrollTop += bottom - (viewH - pad);
    }

    // ---- Events ---------------------------------------------------------------

    function nodeFromEvent(e) {
      var t = e.target;
      while (t && t !== container) {
        if (t.getAttribute && t.getAttribute('data-toggle')) {
          return { id: t.getAttribute('data-toggle'), toggle: true };
        }
        if (t.getAttribute && t.getAttribute('data-id')) {
          return { id: t.getAttribute('data-id'), toggle: false };
        }
        t = t.parentNode;
      }
      return null;
    }

    container.addEventListener('click', function (e) {
      if (dragMoved) { dragMoved = false; return; }
      var hit = nodeFromEvent(e);
      if (!hit) { onBackground(); return; }
      if (hit.toggle) onToggle(hit.id); else onSelect(hit.id);
    });

    container.addEventListener('keydown', function (e) {
      if (!current) return;
      var g = e.target.closest ? e.target.closest('[data-id]') : null;
      if (!g) return;
      var id = g.getAttribute('data-id');
      var target = null;
      var siblings = function () {
        var p = current.parentOf.get(id);
        return p ? current.childrenOf.get(p) : [id];
      };
      switch (e.key) {
        case 'ArrowUp': target = current.parentOf.get(id); break;
        case 'ArrowDown': {
          var kids = current.childrenOf.get(id);
          target = kids && kids[0];
          break;
        }
        case 'ArrowLeft': {
          var sl = siblings();
          target = sl[sl.indexOf(id) - 1];
          break;
        }
        case 'ArrowRight': {
          var sr = siblings();
          target = sr[sr.indexOf(id) + 1];
          break;
        }
        case 'Enter':
        case ' ':
          e.preventDefault();
          if (current.layout.byId.get(id).childCount) onToggle(id); else onSelect(id);
          return;
        case 'Escape':
          return;
        default:
          return;
      }
      e.preventDefault();
      if (target) {
        onSelect(target);
        var next = current && current.nodeEls.get(target);
        if (next) {
          current.nodeEls.forEach(function (n) { n.setAttribute('tabindex', '-1'); });
          next.setAttribute('tabindex', '0');
          next.focus({ preventScroll: true });
          scrollToNode(target);
        }
      }
    });

    container.addEventListener('wheel', function (e) {
      if (!(e.ctrlKey || e.metaKey) || !current) return;
      e.preventDefault();
      var rect = container.getBoundingClientRect();
      setZoom(zoom * Math.exp(-e.deltaY * 0.0015),
        { x: e.clientX - rect.left, y: e.clientY - rect.top });
    }, { passive: false });

    // Drag the background to pan.
    var drag = null;
    var dragMoved = false;
    container.addEventListener('pointerdown', function (e) {
      if (e.button !== 0 || nodeFromEvent(e)) return;
      drag = { x: e.clientX, y: e.clientY, left: container.scrollLeft, top: container.scrollTop };
      dragMoved = false;
      container.setPointerCapture(e.pointerId);
      container.classList.add('panning');
    });
    container.addEventListener('pointermove', function (e) {
      if (!drag) return;
      var dx = e.clientX - drag.x, dy = e.clientY - drag.y;
      if (Math.abs(dx) + Math.abs(dy) > 3) dragMoved = true;
      container.scrollLeft = drag.left - dx;
      container.scrollTop = drag.top - dy;
    });
    function endDrag(e) {
      if (!drag) return;
      drag = null;
      container.classList.remove('panning');
      try { container.releasePointerCapture(e.pointerId); } catch (err) { /* ignore */ }
      setTimeout(function () { dragMoved = false; }, 0);
    }
    container.addEventListener('pointerup', endDrag);
    container.addEventListener('pointercancel', endDrag);

    return {
      render: render,
      clear: clear,
      setSelected: setSelected,
      scrollToNode: scrollToNode,
      getZoom: function () { return zoom; },
      zoomIn: function () { setZoom(zoom * ZOOM_STEP); },
      zoomOut: function () { setZoom(zoom / ZOOM_STEP); },
      // Whole tree visible at once.
      fit: function () {
        if (!current) return;
        var z = Math.min(container.clientWidth / current.layout.width,
          container.clientHeight / current.layout.height);
        setZoom(clamp(z, ZOOM_MIN, 1.5));
        container.scrollLeft = 0;
        container.scrollTop = 0;
      },
      // 100% zoom, scrolled to the root.
      reset: function () {
        if (!current) return;
        zoom = 1;
        applyZoom();
        var rootNode = current.layout.byId.get(current.model.root.id);
        container.scrollTop = 0;
        container.scrollLeft = Math.max(0, rootNode.cx - container.clientWidth / 2);
      },
      layout: function () { return current && current.layout; },
      hasTree: function () { return !!current; }
    };
  }

  var api = { createTreeView: createTreeView, shortLabel: shortLabel };
  root.PTAGEN = root.PTAGEN || {};
  root.PTAGEN.treeview = api;
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
})(typeof window !== 'undefined' ? window : globalThis);
