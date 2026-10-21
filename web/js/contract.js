/* Data contract between the C++ pipeline and the UI (version 1).
 *
 * Documented in docs/frontend-contract.md. This module holds the constants the
 * UI needs and validateResponse(), which every response passes through before
 * it is displayed: a malformed response is reported, never half-rendered.
 */
(function (root) {
  'use strict';

  var CONTRACT_VERSION = 1;

  var TOKEN_TYPES = ['ID', 'NUMBER', 'ASSIGN', 'PLUS', 'MINUS', 'MUL', 'DIV',
    'LPAREN', 'RPAREN', 'SEMICOLON', 'END'];
  var PARSE_SYMBOLS = ['program', 'statement', 'expression', 'term', 'factor', 'terminal'];
  var AST_KINDS = ['Program', 'Assignment', 'Add', 'Subtract', 'Multiply', 'Divide',
    'Identifier', 'Number'];
  var DIAGNOSTIC_KINDS = ['lexical', 'syntax', 'limit'];

  // How a token type reads in an "Expected: ..." list (mirrors the C++ printer).
  var EXPECTED_NAMES = {
    ID: 'identifier', NUMBER: 'number', ASSIGN: "'='", PLUS: "'+'", MINUS: "'-'",
    MUL: "'*'", DIV: "'/'", LPAREN: "'('", RPAREN: "')'", SEMICOLON: "';'",
    END: 'end of input'
  };

  // "a", "a or b", "a, b, or c".
  function describeExpected(types) {
    var names = types.map(function (t) { return EXPECTED_NAMES[t] || t; });
    if (names.length <= 1) return names.join('');
    if (names.length === 2) return names[0] + ' or ' + names[1];
    return names.slice(0, -1).join(', ') + ', or ' + names[names.length - 1];
  }

  // Fixed display captions for AST kinds (a property of the kind, not of the tree).
  var AST_GLYPHS = { Add: '+', Subtract: '-', Multiply: '*', Divide: '/', Assignment: '=' };

  function isInt(n) { return typeof n === 'number' && isFinite(n) && Math.floor(n) === n; }

  // Returns an array of problem descriptions; empty means the response is valid.
  function validateResponse(r) {
    var problems = [];
    function bad(msg) { problems.push(msg); }

    if (!r || typeof r !== 'object') return ['response is not an object'];
    if (r.contractVersion !== CONTRACT_VERSION) {
      bad('unsupported contractVersion ' + JSON.stringify(r.contractVersion));
    }
    if (typeof r.source !== 'string') bad('source must be a string');
    if (typeof r.ok !== 'boolean') bad('ok must be a boolean');
    if (typeof r.tokenized !== 'boolean') bad('tokenized must be a boolean');
    if (!Array.isArray(r.tokens)) bad('tokens must be an array');
    if (!Array.isArray(r.diagnostics)) bad('diagnostics must be an array');
    if (problems.length) return problems;

    r.tokens.forEach(function (t, i) {
      if (t.index !== i) bad('token ' + i + ' has index ' + t.index);
      if (TOKEN_TYPES.indexOf(t.type) < 0) bad('token ' + i + ' has unknown type ' + t.type);
      if (typeof t.lexeme !== 'string') bad('token ' + i + ' lexeme must be a string');
      if (!isInt(t.line) || t.line < 1) bad('token ' + i + ' has invalid line');
      if (!isInt(t.column) || t.column < 1) bad('token ' + i + ' has invalid column');
    });

    r.diagnostics.forEach(function (d, i) {
      if (['lexical', 'syntax'].indexOf(d.phase) < 0) bad('diagnostic ' + i + ' has unknown phase');
      if (DIAGNOSTIC_KINDS.indexOf(d.kind) < 0) bad('diagnostic ' + i + ' has unknown kind');
      // kind 'lexical' <=> phase 'lexical'; 'limit' is a syntax-phase safety limit.
      if ((d.kind === 'lexical') !== (d.phase === 'lexical')) {
        bad('diagnostic ' + i + ' has inconsistent phase and kind');
      }
      if (d.kind === 'limit' && Array.isArray(d.expected) && d.expected.length) {
        bad('diagnostic ' + i + ' is a limit but lists expected tokens');
      }
      if (typeof d.message !== 'string') bad('diagnostic ' + i + ' message must be a string');
      if (!isInt(d.line) || d.line < 1) bad('diagnostic ' + i + ' has invalid line');
      if (!isInt(d.column) || d.column < 1) bad('diagnostic ' + i + ' has invalid column');
      if (typeof d.found !== 'string') bad('diagnostic ' + i + ' found must be a string');
      if (!Array.isArray(d.expected) ||
          d.expected.some(function (e) { return TOKEN_TYPES.indexOf(e) < 0; })) {
        bad('diagnostic ' + i + ' has invalid expected list');
      }
    });

    var ends = r.tokens.filter(function (t) { return t.type === 'END'; }).length;
    if (r.ok) {
      if (!r.tokenized) bad('ok response must be tokenized');
      if (r.diagnostics.length) bad('ok response must have no diagnostics');
      if (!r.parseTree) bad('ok response must have a parseTree');
      if (!r.ast) bad('ok response must have an ast');
      if (ends !== 1 || r.tokens.length === 0 ||
          r.tokens[r.tokens.length - 1].type !== 'END') {
        bad('ok response must end with exactly one END token');
      }
    } else {
      // Invariant from the compiler: a failed analysis never carries a tree.
      if (r.parseTree !== null) bad('failed response must have parseTree null');
      if (r.ast !== null) bad('failed response must have ast null');
      if (!r.diagnostics.length) bad('failed response must have a diagnostic');
      if (r.tokenized && (ends !== 1)) bad('tokenized response must have exactly one END token');
      if (!r.tokenized && ends !== 0) bad('lexing failed but an END token is present');
    }
    if (problems.length) return problems;

    var spanLimit = r.tokens.length - (ends ? 1 : 0);  // END is never covered
    if (r.parseTree) validateTree(r.parseTree, 'parseTree', spanLimit, r.tokens, bad, false);
    if (r.ast) validateTree(r.ast, 'ast', spanLimit, r.tokens, bad, true);
    return problems;
  }

  function validateTree(rootNode, name, spanLimit, tokens, bad, isAst) {
    var ids = Object.create(null);
    var stack = [{ node: rootNode, parent: null }];
    while (stack.length) {
      var item = stack.pop();
      var n = item.node;
      var where = name + ' node ' + (n && n.id);
      if (!n || typeof n !== 'object') { bad(name + ' contains a non-object node'); continue; }
      if (typeof n.id !== 'string' || ids[n.id]) bad(where + ': id missing or duplicated');
      ids[n.id] = true;
      if (!Array.isArray(n.children)) { bad(where + ': children must be an array'); continue; }
      if (!isInt(n.firstToken) || !isInt(n.endToken) || n.firstToken < 0 ||
          n.firstToken > n.endToken || n.endToken > spanLimit) {
        bad(where + ': invalid token span');
      } else if (item.parent && (n.firstToken < item.parent.firstToken ||
                                 n.endToken > item.parent.endToken)) {
        bad(where + ': span lies outside its parent span');
      }
      if (isAst) {
        if (AST_KINDS.indexOf(n.kind) < 0) bad(where + ': unknown kind ' + n.kind);
        var leaf = n.kind === 'Identifier' || n.kind === 'Number';
        if (leaf && (typeof n.value !== 'string' || n.children.length)) {
          bad(where + ': ' + n.kind + ' must have a value and no children');
        }
        if (['Add', 'Subtract', 'Multiply', 'Divide', 'Assignment'].indexOf(n.kind) >= 0 &&
            n.children.length !== 2) {
          bad(where + ': ' + n.kind + ' must have two children');
        }
      } else {
        if (PARSE_SYMBOLS.indexOf(n.symbol) < 0) bad(where + ': unknown symbol ' + n.symbol);
        if (n.symbol === 'terminal') {
          var tok = n.token;
          if (!tok || n.children.length || n.endToken !== n.firstToken + 1 ||
              tok.index !== n.firstToken || !tokens[tok.index] ||
              tokens[tok.index].lexeme !== tok.lexeme || tokens[tok.index].type !== tok.type) {
            bad(where + ': terminal does not match its token');
          }
        }
      }
      for (var i = n.children.length - 1; i >= 0; i--) {
        stack.push({ node: n.children[i], parent: n });
      }
    }
  }

  var api = {
    CONTRACT_VERSION: CONTRACT_VERSION,
    TOKEN_TYPES: TOKEN_TYPES,
    PARSE_SYMBOLS: PARSE_SYMBOLS,
    AST_KINDS: AST_KINDS,
    AST_GLYPHS: AST_GLYPHS,
    describeExpected: describeExpected,
    validateResponse: validateResponse
  };

  root.PTAGEN = root.PTAGEN || {};
  root.PTAGEN.contract = api;
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
})(typeof window !== 'undefined' ? window : globalThis);
