/* Source editor: a <textarea> with a line-number gutter and a highlight layer.
 *
 * The highlight layer is a <pre> underneath the (transparent) textarea that
 * contains the same text, so <mark>-style spans line up exactly with the
 * characters being typed. Both use the same monospace font, line height and
 * padding, no wrapping, and are scroll-synchronised.
 */
(function (root) {
  'use strict';

  var LINE_HEIGHT = 20;

  function escapeHtml(text) {
    return text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }

  // Builds the highlight layer's HTML: `ranges` are { start, end, cls, point }.
  // Overlapping ranges combine their classes; `point` ranges (zero width, e.g.
  // end of input) become a thin marker.
  function buildBackdropHtml(text, ranges) {
    var cuts = new Set([0, text.length]);
    ranges.forEach(function (r) {
      cuts.add(Math.max(0, Math.min(text.length, r.start)));
      cuts.add(Math.max(0, Math.min(text.length, r.end)));
    });
    var points = Array.from(cuts).sort(function (a, b) { return a - b; });
    var html = '';
    var pointMarks = ranges.filter(function (r) { return r.start === r.end; });

    for (var i = 0; i < points.length; i++) {
      var at = points[i];
      pointMarks.forEach(function (m) {
        if (m.start === at) html += '<span class="mk-point mk-' + m.cls + '"></span>';
      });
      if (i === points.length - 1) break;
      var next = points[i + 1];
      var chunk = text.slice(at, next);
      var classes = [];
      ranges.forEach(function (r) {
        if (r.start < r.end && r.start <= at && r.end >= next) classes.push('mk-' + r.cls);
      });
      html += classes.length
        ? '<mark class="' + classes.join(' ') + '">' + escapeHtml(chunk) + '</mark>'
        : escapeHtml(chunk);
    }
    // A trailing newline would otherwise be collapsed and mis-size the layer.
    return html + '\n';
  }

  function createEditor(els, handlers) {
    var textarea = els.textarea;
    var backdrop = els.backdrop;
    var gutter = els.gutter;
    var lastRanges = [];
    var charWidth = 7.8;

    function measureChar() {
      try {
        var canvas = document.createElement('canvas');
        var ctx = canvas.getContext('2d');
        var cs = getComputedStyle(textarea);
        ctx.font = cs.fontSize + ' ' + cs.fontFamily;
        var w = ctx.measureText('M').width;
        if (w > 0) charWidth = w;
      } catch (e) { /* keep default */ }
    }

    function renderGutter(text, errorLine) {
      var count = text.split('\n').length;
      var out = '';
      for (var i = 1; i <= count; i++) {
        out += (i === errorLine ? '<span class="gutter-error">' + i + '</span>' : i) + '\n';
      }
      gutter.innerHTML = out;
      gutter.scrollTop = textarea.scrollTop;
    }

    function syncScroll() {
      backdrop.scrollTop = textarea.scrollTop;
      backdrop.scrollLeft = textarea.scrollLeft;
      gutter.scrollTop = textarea.scrollTop;
    }

    var errorLine = 0;

    function refresh() {
      var text = textarea.value;
      backdrop.innerHTML = buildBackdropHtml(text, lastRanges);
      renderGutter(text, errorLine);
      syncScroll();
    }

    textarea.addEventListener('input', function () {
      // Positions in any earlier analysis no longer apply: drop the marks now.
      lastRanges = [];
      errorLine = 0;
      refresh();
      if (handlers.onInput) handlers.onInput(textarea.value);
    });
    textarea.addEventListener('scroll', syncScroll);
    textarea.addEventListener('keydown', function (e) {
      if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') {
        e.preventDefault();
        if (handlers.onRun) handlers.onRun();
      }
    });
    measureChar();
    refresh();

    return {
      getValue: function () { return textarea.value; },
      // Replaces the text. Does not run analysis and does not fire onInput.
      setValue: function (text) {
        textarea.value = text;
        lastRanges = [];
        errorLine = 0;
        textarea.scrollTop = 0;
        textarea.scrollLeft = 0;
        refresh();
      },
      // ranges: [{ start, end, cls, point? }]; errorLineNumber: 1-based or 0.
      setMarks: function (ranges, errorLineNumber) {
        lastRanges = ranges || [];
        errorLine = errorLineNumber || 0;
        refresh();
      },
      // Scrolls so that offset `start` is visible without moving the caret.
      reveal: function (start) {
        var text = textarea.value;
        var line = 0, lineStart = 0;
        for (var i = 0; i < start && i < text.length; i++) {
          if (text.charCodeAt(i) === 10) { line++; lineStart = i + 1; }
        }
        var top = line * LINE_HEIGHT;
        var viewH = textarea.clientHeight;
        if (top < textarea.scrollTop) textarea.scrollTop = Math.max(0, top - LINE_HEIGHT);
        else if (top + LINE_HEIGHT > textarea.scrollTop + viewH) {
          textarea.scrollTop = Math.max(0, top - viewH / 2);
        }
        var left = (start - lineStart) * charWidth;
        var viewW = textarea.clientWidth;
        if (left < textarea.scrollLeft || left + charWidth * 4 > textarea.scrollLeft + viewW) {
          textarea.scrollLeft = Math.max(0, left - viewW / 2);
        }
        syncScroll();
      },
      focus: function () { textarea.focus(); },
      buildBackdropHtml: buildBackdropHtml
    };
  }

  var api = { createEditor: createEditor, buildBackdropHtml: buildBackdropHtml };
  root.PTAGEN = root.PTAGEN || {};
  root.PTAGEN.editor = api;
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
})(typeof window !== 'undefined' ? window : globalThis);
