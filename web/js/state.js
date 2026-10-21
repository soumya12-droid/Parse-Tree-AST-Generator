/* Application state: a plain object, changed only through the pure functions
 * below (each returns a new state). Rendering code reads state and the
 * derived values here; it never mutates them.
 *
 * Core rule enforced here: results belong to exactly one source text
 * (state.analyzedSource). The moment the editor text differs, results are
 * cleared, so stale trees can never be shown for a different input.
 */
(function (root) {
  'use strict';

  var P = root.PTAGEN || {};
  var contract = P.contract || (typeof require === 'function' ? require('./contract.js') : null);
  var sourcemap = P.sourcemap || (typeof require === 'function' ? require('./sourcemap.js') : null);
  var treemodel = P.treemodel || (typeof require === 'function' ? require('./treemodel.js') : null);

  function initialState() {
    return {
      source: '',
      analyzedSource: null,     // the source the current results belong to
      response: null,           // validated contract response, or null
      provenance: null,         // { kind: 'fixture'|'file', ... }
      outcome: 'idle',          // idle | edited | analyzing | ready | unavailable | error
      message: '',
      errorKind: '',            // for outcome 'error': network | timeout | http | invalid-response
      view: 'both',             // both | parse | ast
      selection: null,          // { tree: 'parse'|'ast', id }
      tokenFocus: null,         // token index chosen in the token table
      models: null,             // { parse, ast } view trees, only when ok
      collapsed: { parse: new Set(), ast: new Set() },
      lineStarts: [0]
    };
  }

  function copy(state, changes) {
    return Object.assign({}, state, changes);
  }

  function clearedResults(state, outcome, message, errorKind) {
    return copy(state, {
      analyzedSource: null, response: null, provenance: null, outcome: outcome,
      message: message || '', errorKind: errorKind || '', selection: null, tokenFocus: null,
      models: null, collapsed: { parse: new Set(), ast: new Set() }
    });
  }

  // The editor text changed (typing, or an example was loaded).
  function sourceChanged(state, source) {
    var next = copy(state, { source: source, lineStarts: sourcemap.lineStarts(source) });
    if (state.outcome !== 'idle' && source !== state.analyzedSource) {
      return clearedResults(next, 'edited');
    }
    return next;
  }

  function withResults(state, response, provenance) {
    var models = null;
    var collapsed = { parse: new Set(), ast: new Set() };
    if (response.ok) {
      models = {
        parse: treemodel.buildViewTree(response.parseTree, 'parse'),
        ast: treemodel.buildViewTree(response.ast, 'ast')
      };
      collapsed = {
        parse: treemodel.defaultCollapsed(models.parse),
        ast: treemodel.defaultCollapsed(models.ast)
      };
    }
    var text = sourcemap.normalizeNewlines(response.source);
    return copy(state, {
      source: text,
      lineStarts: sourcemap.lineStarts(text),
      analyzedSource: text,
      response: response,
      provenance: provenance,
      outcome: 'ready',
      message: '',
      errorKind: '',
      selection: null,
      tokenFocus: null,
      models: models,
      collapsed: collapsed
    });
  }

  function analysisUnavailable(state, message) {
    return clearedResults(state, 'unavailable', message);
  }

  // A request has been sent. Earlier results are cleared at once, so nothing
  // from a previous analysis can be mistaken for the answer to this one.
  function analysisStarted(state) {
    return clearedResults(state, 'analyzing', '');
  }

  // The request itself failed (network, HTTP, malformed response). This is a
  // server/transport problem, never a diagnostic about the user's program.
  function analysisFailed(state, kind, message) {
    return clearedResults(state, 'error', message, kind);
  }

  function select(state, tree, id) {
    if (!state.models || !state.models[tree] || !state.models[tree].byId.has(id)) return state;
    return copy(state, { selection: { tree: tree, id: id }, tokenFocus: null });
  }

  function clearSelection(state) {
    return copy(state, { selection: null, tokenFocus: null });
  }

  function withCollapsed(state, tree, set) {
    var collapsed = { parse: state.collapsed.parse, ast: state.collapsed.ast };
    collapsed[tree] = set;
    return copy(state, { collapsed: collapsed });
  }

  function toggleNode(state, tree, id) {
    if (!state.models) return state;
    var set = new Set(state.collapsed[tree]);
    if (set.has(id)) set.delete(id); else set.add(id);
    var next = withCollapsed(state, tree, set);
    // A selection that is now hidden would be invisible; keep it but it is
    // still described in the details panel. Nothing else to adjust.
    return next;
  }

  function expandAll(state, tree) {
    return withCollapsed(state, tree, new Set());
  }

  // Collapse every node that has children, except the root.
  function collapseAll(state, tree) {
    if (!state.models) return state;
    var model = state.models[tree];
    var set = new Set();
    model.order.forEach(function (v) {
      if (v.children.length && v.id !== model.root.id) set.add(v.id);
    });
    return withCollapsed(state, tree, set);
  }

  // Make sure `id` is visible by expanding its collapsed ancestors.
  function reveal(state, tree, id) {
    var model = state.models[tree];
    var set = new Set(state.collapsed[tree]);
    var changed = false;
    treemodel.ancestorIds(model, id).forEach(function (a) {
      if (set.delete(a)) changed = true;
    });
    return changed ? withCollapsed(state, tree, set) : state;
  }

  function setView(state, view) {
    return copy(state, { view: view });
  }

  // A token row was chosen in the token table. If the visible tree has a leaf
  // covering exactly that token, select it (revealing it); otherwise only the
  // token itself is highlighted.
  function focusToken(state, index, tree) {
    if (!state.response || !state.response.tokens[index]) return state;
    var next = copy(state, { tokenFocus: index, selection: null });
    if (!state.models || !tree || !state.models[tree]) return next;
    var match = null;
    var model = state.models[tree];
    for (var i = 0; i < model.order.length; i++) {
      var v = model.order[i];
      if (!v.children.length && v.first === index && v.end === index + 1) { match = v; break; }
    }
    if (!match) return next;
    next = reveal(next, tree, match.id);
    return copy(next, { selection: { tree: tree, id: match.id }, tokenFocus: null });
  }

  // ---- Derived values ---------------------------------------------------------

  function firstDiagnostic(state) {
    var r = state.response;
    return r && r.diagnostics.length ? r.diagnostics[0] : null;
  }

  function deriveStatus(state) {
    var r = state.response;
    switch (state.outcome) {
      case 'idle':
        return { kind: 'idle', title: 'No analysis yet',
          detail: 'Load an example or type a program, then press Analyze.' };
      case 'edited':
        return { kind: 'edited', title: 'Source changed',
          detail: 'Earlier results were cleared because they no longer match the text. ' +
                  'Press Analyze to analyze the current source.' };
      case 'unavailable':
        return { kind: 'unavailable', title: 'No compiler output for this input',
          detail: state.message };
      case 'analyzing':
        return { kind: 'analyzing', title: 'Analyzing…',
          detail: 'Waiting for the compiler.' };
      case 'error': {
        var titles = { network: 'Cannot reach the server', timeout: 'The server timed out',
          http: 'The server rejected the request', 'invalid-response': 'Unusable server response' };
        return { kind: 'error', title: titles[state.errorKind] || 'Server error',
          detail: state.message };
      }
      default:
        break;
    }
    if (!r) return { kind: 'idle', title: 'No analysis yet', detail: '' };

    if (r.ok) {
      var tokens = r.tokens.length - 1;
      var empty = state.models.ast.root.children.length === 0;
      if (empty) {
        return { kind: 'empty', title: 'Valid empty program',
          detail: 'An empty source is a valid program (program → statement*): ' +
                  'no statements, so the AST is a lone Program node.' };
      }
      var statements = state.models.ast.root.children.length;
      return { kind: 'success', title: 'Parsed successfully',
        detail: statements + (statements === 1 ? ' statement' : ' statements') + ', ' +
                tokens + (tokens === 1 ? ' token' : ' tokens') + ', ' +
                state.models.parse.count + ' parse-tree nodes, ' +
                state.models.ast.count + ' AST nodes.' };
    }
    var d = r.diagnostics[0];
    if (d.kind === 'lexical') return { kind: 'lexical', title: 'Lexical error', detail: '' };
    if (d.kind === 'limit') {
      return { kind: 'limit', title: 'Implementation limit reached',
        detail: 'This is a safety limit of this tool, not a rule of the language: ' +
                'the program is not rejected for breaking the grammar.' };
    }
    return { kind: 'syntax', title: 'Syntax error', detail: '' };
  }

  // Offset ranges to mark in the editor, all from compiler output.
  function highlightRanges(state) {
    var out = [];
    var r = state.response;
    if (!r || state.analyzedSource !== state.source) return out;
    var src = state.analyzedSource;  // newline-normalised, same as the editor text
    var starts = state.lineStarts;

    var d = firstDiagnostic(state);
    if (d) {
      var range = sourcemap.diagnosticRange(src, starts, d);
      if (range) {
        out.push({ start: range.start, end: range.end, cls: 'error',
          point: range.start === range.end });
      }
    }
    if (state.selection && state.models) {
      var view = state.models[state.selection.tree].byId.get(state.selection.id);
      if (view) {
        var span = sourcemap.spanRange(src, starts, r.tokens, view.first, view.end);
        if (span) out.push({ start: span.start, end: span.end, cls: 'node' });
      }
    }
    if (state.tokenFocus !== null) {
      var tr = sourcemap.tokenRange(src, starts, r.tokens[state.tokenFocus]);
      if (tr) out.push({ start: tr.start, end: tr.end, cls: 'node' });
    }
    return out;
  }

  // Which token rows to mark in the token table.
  function tokenMarks(state) {
    var marks = { selected: new Set(), error: -1 };
    var r = state.response;
    if (!r) return marks;
    if (state.selection && state.models) {
      var v = state.models[state.selection.tree].byId.get(state.selection.id);
      if (v) for (var i = v.first; i < v.end; i++) marks.selected.add(i);
    }
    if (state.tokenFocus !== null) marks.selected.add(state.tokenFocus);
    var d = firstDiagnostic(state);
    if (d && r.tokenized) marks.error = sourcemap.tokenIndexAt(r.tokens, d.line, d.column);
    return marks;
  }

  // Details of the selected node, from the model and the compiler's spans.
  function selectionDetails(state) {
    if (!state.selection || !state.models || !state.response) return null;
    var sel = state.selection;
    var model = state.models[sel.tree];
    var v = model.byId.get(sel.id);
    if (!v) return null;

    var role;
    if (sel.tree === 'parse') {
      role = v.role === 'terminal' ? 'Terminal (token)' : 'Nonterminal (grammar rule)';
    } else {
      role = v.role === 'leaf' ? 'AST leaf' : (v.cls === 'ast-structure' ? 'AST structure' : 'AST operation');
    }
    var raw = v.raw;
    var location = sourcemap.spanLocation(state.analyzedSource, state.lineStarts,
      state.response.tokens, v.first, v.end);
    var descendants = 0;
    var stack = v.children.slice();
    while (stack.length) {
      var n = stack.pop();
      descendants++;
      for (var i = 0; i < n.children.length; i++) stack.push(n.children[i]);
    }
    return {
      tree: sel.tree,
      treeName: sel.tree === 'parse' ? 'Parse tree' : 'AST',
      id: v.id,
      name: sel.tree === 'parse'
        ? (raw.symbol === 'terminal' ? raw.token.type : raw.symbol)
        : raw.kind,
      value: sel.tree === 'parse'
        ? (raw.symbol === 'terminal' ? raw.token.lexeme : null)
        : (raw.value !== undefined ? raw.value : null),
      role: role,
      first: v.first,
      end: v.end,
      tokenCount: v.end - v.first,
      location: location,
      childCount: v.children.length,
      descendantCount: descendants,
      depth: model.depth.get(v.id)
    };
  }

  var api = {
    initialState: initialState,
    sourceChanged: sourceChanged,
    withResults: withResults,
    analysisUnavailable: analysisUnavailable,
    analysisStarted: analysisStarted,
    analysisFailed: analysisFailed,
    select: select,
    clearSelection: clearSelection,
    toggleNode: toggleNode,
    expandAll: expandAll,
    collapseAll: collapseAll,
    reveal: reveal,
    setView: setView,
    focusToken: focusToken,
    deriveStatus: deriveStatus,
    firstDiagnostic: firstDiagnostic,
    highlightRanges: highlightRanges,
    tokenMarks: tokenMarks,
    selectionDetails: selectionDetails
  };

  root.PTAGEN = root.PTAGEN || {};
  root.PTAGEN.state = api;
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
})(typeof window !== 'undefined' ? window : globalThis);
