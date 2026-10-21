/* Maps compiler output positions to text in the editor.
 *
 * Everything here is derived from the data the compiler produced: token
 * line/column positions and [firstToken, endToken) spans (endToken exclusive).
 * Nothing is guessed. Line breaks follow the C++ lexer: "\r\n", a lone "\r"
 * and "\n" each end a line. Columns count code points (tab = 1).
 */
(function (root) {
  'use strict';

  // A <textarea> always reports "\n" line breaks, whatever the file used. The
  // UI therefore works on newline-normalised text. This changes no line or
  // column: "\r\n" and a lone "\r" are each one line break, exactly as in the
  // lexer, and no token spans a line break.
  function normalizeNewlines(text) {
    return text.replace(/\r\n?/g, '\n');
  }

  // Offset (UTF-16 index) at which each line starts.
  function lineStarts(source) {
    var starts = [0];
    for (var i = 0; i < source.length; i++) {
      var c = source.charCodeAt(i);
      if (c === 13) {                       // \r
        if (source.charCodeAt(i + 1) === 10) i++;
        starts.push(i + 1);
      } else if (c === 10) {                // \n
        starts.push(i + 1);
      }
    }
    return starts;
  }

  // Number of UTF-16 units spanned by `codePoints` code points starting at `from`.
  function advanceCodePoints(source, from, codePoints) {
    var i = from;
    for (var n = 0; n < codePoints && i < source.length; n++) {
      var c = source.charCodeAt(i);
      i += (c >= 0xD800 && c <= 0xDBFF && i + 1 < source.length) ? 2 : 1;
    }
    return i;
  }

  // 1-based (line, column) -> offset, or -1 if the position does not exist.
  function positionToOffset(source, starts, line, column) {
    if (line < 1 || line > starts.length || column < 1) return -1;
    var lineStart = starts[line - 1];
    var offset = advanceCodePoints(source, lineStart, column - 1);
    // The column must lie on this line (or at its very end).
    var lineEnd = line < starts.length ? starts[line] : source.length;
    if (offset > lineEnd) return -1;
    return offset;
  }

  function codePointLength(text) {
    var n = 0;
    for (var i = 0; i < text.length; i++) {
      var c = text.charCodeAt(i);
      if (c >= 0xD800 && c <= 0xDBFF && i + 1 < text.length) i++;
      n++;
    }
    return n;
  }

  // Offset range of one token, or null (END token, or position not in source).
  function tokenRange(source, starts, token) {
    if (!token || token.type === 'END' || token.lexeme === '') return null;
    var start = positionToOffset(source, starts, token.line, token.column);
    if (start < 0) return null;
    return { start: start, end: start + token.lexeme.length };
  }

  // Token span [first, end) -> offset range covering those tokens, or null for
  // an empty span (e.g. the empty program) or one that cannot be mapped.
  function spanRange(source, starts, tokens, first, end) {
    if (!(first < end)) return null;
    var a = tokenRange(source, starts, tokens[first]);
    var b = tokenRange(source, starts, tokens[end - 1]);
    if (!a || !b) return null;
    return { start: a.start, end: b.end };
  }

  // Human-readable location of a token span, from the tokens' own positions.
  // endColumn is inclusive. Returns null for an empty span.
  function spanLocation(source, starts, tokens, first, end) {
    var range = spanRange(source, starts, tokens, first, end);
    if (!range) return null;
    var a = tokens[first];
    var b = tokens[end - 1];
    return {
      startLine: a.line,
      startColumn: a.column,
      endLine: b.line,
      endColumn: b.column + codePointLength(b.lexeme) - 1,
      text: source.slice(range.start, range.end)
    };
  }

  // Index of the token starting at (line, column), or -1.
  function tokenIndexAt(tokens, line, column) {
    for (var i = 0; i < tokens.length; i++) {
      if (tokens[i].line === line && tokens[i].column === column) return i;
    }
    return -1;
  }

  // Source offset of a diagnostic: a range over the offending text, or a
  // zero-width position (found === '') such as end of input.
  function diagnosticRange(source, starts, diagnostic) {
    var start = positionToOffset(source, starts, diagnostic.line, diagnostic.column);
    if (start < 0) return null;
    return { start: start, end: start + diagnostic.found.length };
  }

  var api = {
    normalizeNewlines: normalizeNewlines,
    lineStarts: lineStarts,
    positionToOffset: positionToOffset,
    codePointLength: codePointLength,
    tokenRange: tokenRange,
    spanRange: spanRange,
    spanLocation: spanLocation,
    tokenIndexAt: tokenIndexAt,
    diagnosticRange: diagnosticRange
  };

  root.PTAGEN = root.PTAGEN || {};
  root.PTAGEN.sourcemap = api;
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
})(typeof window !== 'undefined' ? window : globalThis);
