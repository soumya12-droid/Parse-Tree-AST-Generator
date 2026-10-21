/* Application wiring: DOM events -> state changes -> rendering.
 *
 * All decisions about *what* is shown live in state.js (pure, unit-tested);
 * this file only connects the browser to it.
 */
(function () {
  'use strict';

  var P = window.PTAGEN;
  var S = P.state;
  var sourcemap = P.sourcemap;
  var treemodel = P.treemodel;

  var $ = function (id) { return document.getElementById(id); };

  // Offline adapter: captured real compiler output (always available).
  var fixtureAdapter = P.analyzer.createFixtureAnalyzer({
    fixtures: window.PTAGEN_FIXTURES || [],
    loadStressResponses: function () {
      if (window.PTAGEN_STRESS_RESPONSES) return Promise.resolve(window.PTAGEN_STRESS_RESPONSES);
      return P.analyzer.loadScriptOnce('fixtures/fixtures-stress.js').then(function () {
        return window.PTAGEN_STRESS_RESPONSES;
      });
    }
  });

  // Mode: LIVE (the page was served over HTTP by `ptagen serve`) or OFFLINE
  // fixtures (opened from a file, or forced with ?mode=fixtures).
  var served = location.protocol === 'http:' || location.protocol === 'https:';
  var forcedOffline = new URLSearchParams(location.search).get('mode') === 'fixtures';
  var live = served && !forcedOffline;
  var adapter = live ? P.analyzer.createHttpAnalyzer({}) : fixtureAdapter;

  // The example menu is just sample input text; it is the same in both modes.
  var examples = fixtureAdapter.examples();

  var state = S.initialState();
  var requestId = 0;
  var pendingExample = null;
  var lastToggled = null;
  var lastRevealKey = '';

  // ---- Elements ---------------------------------------------------------------------

  var editor = P.editor.createEditor(
    { textarea: $('source'), backdrop: $('backdrop'), gutter: $('gutter') },
    {
      onInput: function (value) {
        // "Example X is loaded" no longer describes what is in the editor.
        $('example-note').textContent = '';
        setState(S.sourceChanged(state, value));
      },
      onRun: analyze
    });

  var panes = {
    parse: makePane('parse'),
    ast: makePane('ast')
  };

  function makePane(kind) {
    var root = $('pane-' + kind);
    var pane = {
      kind: kind,
      root: root,
      scroll: root.querySelector('.tree-scroll'),
      empty: root.querySelector('.pane-empty'),
      note: root.querySelector('.pane-note'),
      zoomLabel: root.querySelector('[data-zoom-label]'),
      rendered: { model: null, collapsed: null }
    };
    pane.view = P.treeview.createTreeView({
      container: pane.scroll,
      kind: kind,
      onSelect: function (id) { setState(S.select(state, kind, id)); },
      onToggle: function (id) {
        lastToggled = id;
        setState(S.toggleNode(state, kind, id));
      },
      onBackground: function () { setState(S.clearSelection(state)); },
      onZoom: function (z) { pane.zoomLabel.textContent = Math.round(z * 100) + '%'; }
    });
    root.addEventListener('click', function (e) {
      var btn = e.target.closest ? e.target.closest('[data-action]') : null;
      if (!btn) return;
      switch (btn.getAttribute('data-action')) {
        case 'zoom-in': pane.view.zoomIn(); break;
        case 'zoom-out': pane.view.zoomOut(); break;
        case 'fit': pane.view.fit(); break;
        case 'reset': pane.view.reset(); break;
        case 'expand-all': lastToggled = null; setState(S.expandAll(state, kind)); break;
        case 'collapse-all': lastToggled = null; setState(S.collapseAll(state, kind)); break;
        default: break;
      }
    });
    return pane;
  }

  // ---- State -----------------------------------------------------------------------------

  function setState(next) {
    state = next;
    render();
  }

  function activeTree() {
    return state.view === 'ast' ? 'ast' : 'parse';
  }

  // ---- Actions -----------------------------------------------------------------------------

  function analyze() {
    var source = state.source;
    var id = ++requestId;
    $('analyze').disabled = true;
    // Clear any earlier result before asking, so it cannot pass for this one.
    setState(S.analysisStarted(state));
    adapter.analyze(source).then(function (outcome) {
      // Ignore answers for text the user has since changed or re-requested.
      if (id !== requestId || state.source !== source) {
        if (id === requestId) $('analyze').disabled = false;
        return;
      }
      $('analyze').disabled = false;
      if (outcome.status === 'ok') {
        lastRevealKey = '';
        setState(S.withResults(state, outcome.response, outcome.provenance));
        if (live) setBadge('ok');
      } else if (outcome.status === 'error') {
        setState(S.analysisFailed(state, outcome.kind, outcome.message));
        if (live && (outcome.kind === 'network' || outcome.kind === 'timeout')) setBadge('down');
      } else {
        setState(S.analysisUnavailable(state, outcome.message));
      }
    });
  }

  function applyExample(example) {
    editor.setValue(example.source);
    pendingExample = null;
    $('replace-confirm').hidden = true;
    $('example-note').textContent = 'Loaded "' + example.title + '". ' + example.description +
      ' Press Analyze to see the result.';
    setState(S.sourceChanged(state, editor.getValue()));
    editor.focus();
  }

  function isKnownSource(text) {
    var t = sourcemap.normalizeNewlines(text);
    return examples.some(function (ex) { return sourcemap.normalizeNewlines(ex.source) === t; });
  }

  function requestExample(id) {
    var example = examples.filter(function (e) { return e.id === id; })[0];
    if (!example) return;
    var current = editor.getValue();
    var same = sourcemap.normalizeNewlines(example.source) === current;
    if (current.trim() !== '' && !same && !isKnownSource(current)) {
      // The editor holds the user's own text: ask before replacing it.
      pendingExample = example;
      $('replace-confirm-text').textContent =
        'Replace the text in the editor with the example "' + example.title + '"?';
      $('replace-confirm').hidden = false;
      return;
    }
    applyExample(example);
  }

  function openResultFile(file) {
    var reader = new FileReader();
    reader.onload = function () {
      var outcome = fixtureAdapter.loadResultText(String(reader.result), file.name);
      if (outcome.status === 'ok') {
        lastRevealKey = '';
        var next = S.withResults(state, outcome.response, outcome.provenance);
        editor.setValue(next.source);
        $('example-note').textContent = 'Loaded analysis result from "' + file.name + '".';
        setState(next);
      } else {
        // Leave the current source and results untouched; just say why.
        $('example-note').textContent = 'Result file "' + file.name + '" was not used. ' +
          outcome.message;
      }
    };
    reader.onerror = function () {
      $('example-note').textContent = 'Could not read "' + file.name + '".';
    };
    reader.readAsText(file);
  }

  // ---- Rendering --------------------------------------------------------------------------------

  function render() {
    var status = S.deriveStatus(state);

    // Editor marks (error range, selected node range).
    var ranges = S.highlightRanges(state);
    var diag = S.firstDiagnostic(state);
    var errorLine = state.response && diag && state.analyzedSource === state.source ? diag.line : 0;
    editor.setMarks(ranges, errorLine);
    var nodeRange = ranges.filter(function (r) { return r.cls === 'node'; })[0];
    var revealKey = nodeRange ? nodeRange.start + ':' + nodeRange.end : '';
    if (revealKey && revealKey !== lastRevealKey) editor.reveal(nodeRange.start);
    lastRevealKey = revealKey;

    // Status / diagnostics.
    var statusEl = $('status');
    P.panels.renderStatus(statusEl, status, state, S);
    // Live results need no caption (the header badge says "Live"); captured
    // fixture/file results keep theirs so they are never mistaken for live ones.
    if (state.outcome === 'ready' && state.provenance && state.provenance.kind !== 'live') {
      var prov = document.createElement('p');
      prov.className = 'status-prov';
      prov.textContent = P.panels.provenanceText(state.provenance) +
        (state.provenance.kind === 'live'
          ? '.'
          : '. Captured offline from the C++ pipeline; not a live analysis.');
      statusEl.appendChild(prov);
    }

    // Tokens.
    P.panels.renderTokens($('tokens'), state.outcome === 'ready' ? state.response : null,
      S.tokenMarks(state), function (index) {
        setState(S.focusToken(state, index, activeTree()));
      });
    var tokenCount = state.outcome === 'ready' && state.response ? state.response.tokens.length : 0;
    $('token-count').textContent = tokenCount ? String(tokenCount) : '';

    // Trees.
    ['parse', 'ast'].forEach(function (kind) { renderPane(panes[kind], status); });

    // View switch.
    $('panes').setAttribute('data-view', state.view);
    Array.prototype.forEach.call(document.querySelectorAll('[data-view-btn]'), function (btn) {
      btn.setAttribute('aria-pressed',
        String(btn.getAttribute('data-view-btn') === state.view));
    });

    // Details.
    P.panels.renderDetails($('details'), S.selectionDetails(state), !!state.models, function () {
      setState(S.clearSelection(state));
    });

    // Keep the selected node in view. This must come AFTER the details panel
    // is rendered: showing details makes that panel taller and the tree area
    // shorter, and measuring before that left nodes hidden at the bottom edge.
    if (state.selection) {
      var selPane = panes[state.selection.tree];
      if (selPane.view.hasTree()) selPane.view.scrollToNode(state.selection.id);
    }
  }

  var EMPTY_TEXT = {
    analyzing: 'Waiting for the compiler…',
    error: 'No tree: the analysis request failed.',
    idle: 'Run an analysis to see the tree.',
    edited: 'The source changed. Press Analyze to see its tree.',
    unavailable: 'No tree: there is no compiler output for this input.',
    lexical: 'No tree: lexical analysis failed, so nothing was parsed.',
    syntax: 'No tree: the input has a syntax error.',
    limit: 'No tree: the input exceeds an implementation limit.'
  };

  function renderPane(pane, status) {
    var kind = pane.kind;
    var model = state.models ? state.models[kind] : null;

    if (!model) {
      pane.view.clear();
      pane.rendered = { model: null, collapsed: null };
      pane.empty.hidden = false;
      pane.empty.textContent = EMPTY_TEXT[status.kind] || EMPTY_TEXT.idle;
      pane.note.textContent = '';
      pane.zoomLabel.textContent = '';
      setControlsDisabled(pane, true);
      return;
    }

    pane.empty.hidden = true;
    setControlsDisabled(pane, false);
    var collapsed = state.collapsed[kind];
    var selectedId = state.selection && state.selection.tree === kind ? state.selection.id : null;
    var subtree = selectedId ? treemodel.descendantIds(model, selectedId) : [];

    var fresh = pane.rendered.model !== model;
    var changed = fresh || pane.rendered.collapsed !== collapsed;
    if (changed) {
      pane.view.render(model, collapsed, selectedId,
        { fresh: fresh, anchor: fresh ? null : lastToggled, selectedSubtree: subtree });
    } else {
      pane.view.setSelected(selectedId, subtree);
    }
    pane.rendered = { model: model, collapsed: collapsed };
    lastToggled = null;

    var hiddenSubtrees = collapsed.size;
    pane.note.textContent = model.count + ' nodes' +
      (hiddenSubtrees ? ' · ' + hiddenSubtrees + ' collapsed subtree' +
        (hiddenSubtrees === 1 ? '' : 's') + ' (click + to expand)' : '');
  }

  function setControlsDisabled(pane, disabled) {
    Array.prototype.forEach.call(pane.root.querySelectorAll('[data-action]'), function (b) {
      b.disabled = disabled;
    });
  }

  // ---- Static UI wiring -----------------------------------------------------------------------------

  function buildExampleMenu() {
    var select = $('example-select');
    // One flat list: no category headings, every example in its original order.
    examples.forEach(function (ex) {
      var opt = document.createElement('option');
      opt.value = ex.id;
      opt.textContent = ex.title === '' ? '(empty)' : ex.title;
      select.appendChild(opt);
    });
    select.addEventListener('change', function () {
      var id = select.value;
      select.value = '';
      if (id) requestExample(id);
    });
  }

  $('analyze').addEventListener('click', analyze);
  $('clear-source').addEventListener('click', function () {
    editor.setValue('');
    $('example-note').textContent = '';
    setState(S.sourceChanged(state, ''));
    editor.focus();
  });
  $('replace-yes').addEventListener('click', function () {
    if (pendingExample) applyExample(pendingExample);
  });
  $('replace-no').addEventListener('click', function () {
    pendingExample = null;
    $('replace-confirm').hidden = true;
  });
  $('open-file').addEventListener('click', function () { $('file-input').click(); });
  $('file-input').addEventListener('change', function () {
    var file = $('file-input').files[0];
    $('file-input').value = '';
    if (file) openResultFile(file);
  });
  Array.prototype.forEach.call(document.querySelectorAll('[data-view-btn]'), function (btn) {
    btn.addEventListener('click', function () {
      setState(S.setView(state, btn.getAttribute('data-view-btn')));
    });
  });
  document.addEventListener('keydown', function (e) {
    if (e.key === 'Escape' && state.selection) setState(S.clearSelection(state));
  });

  // Header badge. Live mode never claims "live" until the server has answered.
  function setBadge(kind) {
    var el = $('provenance');
    var text = {
      checking: 'Live mode · contacting the local server…',
      ok: 'Live',
      down: 'Live mode · server not reachable',
      offline: 'Offline · captured compiler output, no live compiler',
      forced: 'Offline (?mode=fixtures) · captured compiler output'
    }[kind];
    el.textContent = text;
    el.className = 'provenance is-' + (kind === 'ok' ? 'live' : kind === 'down' ? 'down' : kind === 'checking' ? 'checking' : 'offline');
  }

  if (live) {
    setBadge('checking');
    $('open-file').hidden = true;          // result files are an offline-mode feature
    adapter.health().then(function (h) {
      if (h.ok) setBadge('ok');
      else { setBadge('down'); $('example-note').textContent = h.message; }
    });
  } else {
    setBadge(served ? 'forced' : 'offline');
  }

  buildExampleMenu();
  // Start with the first example in the editor, not analyzed.
  if (examples.length) {
    editor.setValue(examples[0].source);
    $('example-note').textContent = 'Example "' + examples[0].title +
      '" is loaded. Press Analyze to see the result.';
    state = S.sourceChanged(state, editor.getValue());
  }
  render();

  // Test hook (read-only view of state) for in-browser verification.
  window.PTAGEN_DEBUG = { getState: function () { return state; }, mode: live ? 'live' : 'fixtures' };
})();
