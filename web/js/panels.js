/* DOM rendering for the non-tree panels: status/diagnostics, token table and
 * node details. Each function takes data and writes into one container; none
 * of them holds state.
 */
(function (root) {
  'use strict';

  var P = root.PTAGEN;

  function h(tag, className, text) {
    var node = document.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined && text !== null) node.textContent = text;
    return node;
  }

  function codePoints(text) { return Array.from(text); }

  // ---- Status and diagnostics ----------------------------------------------------

  var STATUS_LABEL = {
    idle: 'Ready', edited: 'Edited', unavailable: 'Unavailable', analyzing: 'Working',
    error: 'Server problem', success: 'Success',
    empty: 'Valid', lexical: 'Lexical', syntax: 'Syntax', limit: 'Limit'
  };

  // Source lines around the error, with a caret under the offending text.
  function errorContext(source, diagnostic) {
    var lines = source.split('\n');
    var out = h('pre', 'context');
    out.setAttribute('aria-label', 'Source context of the error');
    var from = Math.max(1, diagnostic.line - 1);
    var to = Math.min(lines.length, diagnostic.line + 1);
    var width = String(to).length;
    for (var n = from; n <= to; n++) {
      var text = lines[n - 1].replace(/\t/g, ' ');
      var shown = text.length > 160 ? text.slice(0, 157) + '...' : text;
      var prefix = String(n);
      while (prefix.length < width) prefix = ' ' + prefix;
      out.appendChild(h('span', n === diagnostic.line ? 'ctx-line ctx-hit' : 'ctx-line',
        prefix + ' | ' + shown + '\n'));
      if (n === diagnostic.line) {
        var col = Math.min(diagnostic.column - 1, 157);
        var carets = Math.max(1, codePoints(diagnostic.found).length);
        var pad = ' ';
        for (var i = 0; i < width; i++) pad += ' ';
        out.appendChild(h('span', 'ctx-caret',
          pad + '  ' + new Array(col + 1).join(' ') + new Array(carets + 1).join('^') + '\n'));
      }
    }
    return out;
  }

  function renderStatus(container, status, state, derive) {
    container.textContent = '';
    container.className = 'panel status-panel status-' + status.kind;

    var head = h('div', 'status-head');
    head.appendChild(h('span', 'status-badge', STATUS_LABEL[status.kind] || status.kind));
    head.appendChild(h('span', 'status-title', status.title));
    container.appendChild(head);

    var d = derive.firstDiagnostic(state);
    var isError = ['lexical', 'syntax', 'limit'].indexOf(status.kind) >= 0;
    if (isError && d) {
      if (status.detail) container.appendChild(h('p', 'status-detail', status.detail));
      var msg = h('p', 'status-message');
      msg.appendChild(h('span', 'msg-text', d.message));
      msg.appendChild(h('span', 'msg-loc', 'line ' + d.line + ', column ' + d.column));
      container.appendChild(msg);
      if (d.expected.length) {
        var exp = h('p', 'status-expected');
        exp.appendChild(h('span', 'exp-label', 'Expected '));
        exp.appendChild(document.createTextNode(P.contract.describeExpected(d.expected)));
        container.appendChild(exp);
      }
      container.appendChild(errorContext(state.analyzedSource, d));
      var note = status.kind === 'lexical'
        ? 'No parse tree or AST: lexing failed, so the parser never ran.'
        : 'No parse tree or AST is shown for input that was not accepted.';
      container.appendChild(h('p', 'status-note', note));
    } else if (status.detail) {
      container.appendChild(h('p', 'status-detail', status.detail));
      if (status.kind === 'error') {
        container.appendChild(h('p', 'status-note',
          'This is not a problem with your program. Nothing from an earlier analysis is shown.'));
      }
    }
  }

  // ---- Provenance ----------------------------------------------------------------------

  function provenanceText(provenance) {
    if (!provenance) return '';
    if (provenance.kind === 'live') return 'Live result from the C++ analyzer (local server)';
    if (provenance.kind === 'file') return 'Result file: ' + provenance.name;
    return 'Captured output for example "' + provenance.title + '"';
  }

  // ---- Token table -----------------------------------------------------------------------

  function renderTokens(container, response, marks, onPick) {
    container.textContent = '';
    if (!response) {
      container.appendChild(h('p', 'empty', 'Tokens appear here after analysis.'));
      return;
    }
    if (!response.tokenized) {
      container.appendChild(h('p', 'token-note',
        'Lexing failed. Only the tokens recognised around the invalid characters are listed; ' +
        'there is no END token and the parser did not run.'));
    }
    if (response.tokens.length === 0) {
      container.appendChild(h('p', 'empty', 'No tokens were produced.'));
      return;
    }
    var table = h('table', 'token-table');
    var thead = h('thead');
    var hr = h('tr');
    ['#', 'Type', 'Lexeme', 'Line', 'Col'].forEach(function (name) {
      var th = h('th', null, name);
      th.setAttribute('scope', 'col');
      hr.appendChild(th);
    });
    thead.appendChild(hr);
    table.appendChild(thead);

    var tbody = h('tbody');
    response.tokens.forEach(function (t, i) {
      var tr = h('tr');
      tr.setAttribute('data-index', String(i));
      if (marks.selected.has(i)) tr.className = 'is-selected';
      if (marks.error === i) tr.className += ' is-error';
      tr.appendChild(h('td', 'num', String(i)));
      var typeCell = h('td', 'type');
      typeCell.appendChild(h('span', 'type-chip type-' + t.type.toLowerCase(), t.type));
      tr.appendChild(typeCell);
      tr.appendChild(t.type === 'END'
        ? h('td', 'lexeme lexeme-end', '(end of input)')
        : h('td', 'lexeme', t.lexeme));
      tr.appendChild(h('td', 'num', String(t.line)));
      tr.appendChild(h('td', 'num', String(t.column)));
      tr.addEventListener('click', function () { onPick(i); });
      tbody.appendChild(tr);
    });
    table.appendChild(tbody);
    container.appendChild(table);

    // Bring the first marked row into view inside the table's own scroller
    // (scrollIntoView would also scroll the whole page).
    var firstMarked = tbody.querySelector('.is-selected, .is-error');
    if (firstMarked) {
      var box = container.getBoundingClientRect();
      var rowBox = firstMarked.getBoundingClientRect();
      var headerH = thead.getBoundingClientRect().height;
      var rowTop = rowBox.top - box.top + container.scrollTop;
      if (rowTop - headerH < container.scrollTop) {
        container.scrollTop = Math.max(0, rowTop - headerH);
      } else if (rowTop + rowBox.height > container.scrollTop + container.clientHeight) {
        container.scrollTop = rowTop + rowBox.height - container.clientHeight;
      }
    }
  }

  // ---- Node details ------------------------------------------------------------------------

  function renderDetails(container, details, hasTrees, onClear) {
    container.textContent = '';
    if (!details) {
      container.appendChild(h('div', 'panel-head')).appendChild(h('h2', null, 'Node details'));
      container.appendChild(h('p', 'empty', hasTrees
        ? 'Select a node in either tree to see its token span and source range.'
        : 'Node details appear here when a tree node is selected.'));
      return;
    }
    var head = h('div', 'panel-head');
    head.appendChild(h('h2', null, 'Node details'));
    var clear = h('button', 'btn btn-quiet btn-small', 'Clear selection');
    clear.type = 'button';
    clear.addEventListener('click', onClear);
    head.appendChild(clear);
    container.appendChild(head);

    var dl = h('dl', 'details-grid');
    function row(label, value, mono) {
      var kv = h('div', 'kv');
      kv.appendChild(h('dt', null, label));
      kv.appendChild(h('dd', mono ? 'mono' : null, value));
      dl.appendChild(kv);
    }
    row('Tree', details.treeName);
    row(details.tree === 'parse' ? 'Symbol' : 'Kind', details.name, true);
    if (details.value !== null) row('Value', details.value, true);
    row('Role', details.role);
    row('Token span', '[' + details.first + ', ' + details.end + ')  •  ' +
      details.tokenCount + (details.tokenCount === 1 ? ' token' : ' tokens') +
      '  (end is exclusive)', true);
    if (details.location) {
      var loc = details.location;
      var where = loc.startLine === loc.endLine
        ? 'line ' + loc.startLine + ', columns ' + loc.startColumn + '–' + loc.endColumn
        : 'line ' + loc.startLine + ' col ' + loc.startColumn + ' to line ' + loc.endLine +
          ' col ' + loc.endColumn;
      row('Source range', where);
      var text = loc.text.length > 90 ? loc.text.slice(0, 87) + '...' : loc.text;
      row('Source text', text, true);
    } else {
      row('Source range', 'none (empty span: no tokens)');
    }
    row('Children', details.childCount + (details.childCount ? '  (' + details.descendantCount +
      ' nodes below)' : ''));
    container.appendChild(dl);
  }

  var api = {
    renderStatus: renderStatus,
    renderTokens: renderTokens,
    renderDetails: renderDetails,
    provenanceText: provenanceText
  };
  root.PTAGEN = root.PTAGEN || {};
  root.PTAGEN.panels = api;
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
})(typeof window !== 'undefined' ? window : globalThis);
