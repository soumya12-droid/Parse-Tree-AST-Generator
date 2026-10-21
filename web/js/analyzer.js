/* Analysis adapter: the one place the UI asks "what did the compiler say about
 * this source?". Everything else in the UI consumes the result through the
 * data contract (docs/frontend-contract.md) and never knows where it came from.
 *
 * Adapter interface:
 *
 *   adapter.live                   boolean - true only for a real, running compiler
 *   adapter.label                  short text describing the data source
 *   adapter.analyze(source)        Promise<Outcome>
 *
 *   Outcome = { status: 'ok', response, provenance }
 *           | { status: 'unavailable', message }                    (offline: no captured output)
 *           | { status: 'error', kind, message, httpStatus? }       (live: request failed)
 *
 * Two adapters exist:
 *
 *  - createHttpAnalyzer: LIVE. POSTs { source } to the local server
 *    (POST /api/analyze) and returns the real C++ analyzer's contract JSON.
 *    Compiler diagnostics are ordinary 'ok' outcomes (response.ok === false);
 *    'error' is reserved for transport, HTTP and malformed-response failures,
 *    so "your program is wrong" is never confused with "the server failed".
 *
 *  - createFixtureAnalyzer: OFFLINE. Answers only for sources whose real
 *    compiler output was captured earlier (tools/export_fixtures.cpp) or that
 *    the user loaded from a result file. It computes nothing itself and always
 *    reports live === false.
 *
 * app.js chooses one; everything downstream only sees contract responses.
 */
(function (root) {
  'use strict';

  var contract = (root.PTAGEN && root.PTAGEN.contract) ||
    (typeof require === 'function' ? require('./contract.js') : null);
  var sourcemap = (root.PTAGEN && root.PTAGEN.sourcemap) ||
    (typeof require === 'function' ? require('./sourcemap.js') : null);
  var norm = function (s) { return sourcemap.normalizeNewlines(s); };

  var UNAVAILABLE_HINT =
    'Offline mode: only compiler output captured earlier can be shown, and none exists for ' +
    'this text. Pick an example, or start the local server (scripts\\run.ps1) and open ' +
    'http://127.0.0.1:8080/ to analyze any program live.';

  function createFixtureAnalyzer(options) {
    var fixtures = options.fixtures || [];
    var loadStress = options.loadStressResponses || function () {
      return Promise.reject(new Error('stress fixtures are not available'));
    };

    var bySource = new Map();
    // Sources are compared newline-normalised: the editor cannot represent
    // "\r\n", and it changes no token position (see sourcemap.normalizeNewlines).
    fixtures.forEach(function (f) {
      if (!bySource.has(norm(f.source))) bySource.set(norm(f.source), f);
    });
    var fromFiles = new Map();  // source -> { response, name }

    function ok(response, provenance) {
      return { status: 'ok', response: response, provenance: provenance };
    }

    function checked(response, source, provenance) {
      var problems = contract.validateResponse(response);
      if (problems.length) {
        return { status: 'unavailable',
          message: 'The captured result is malformed and was not displayed: ' + problems[0] };
      }
      if (norm(response.source) !== norm(source)) {
        return { status: 'unavailable',
          message: 'The captured result does not belong to this source and was not displayed.' };
      }
      return ok(response, provenance);
    }

    return {
      id: 'fixtures',
      live: false,
      label: 'Captured compiler output (offline) - no live compiler connected',

      examples: function () {
        return fixtures.map(function (f) {
          return { id: f.id, group: f.group, title: f.title, description: f.description,
            source: f.source };
        });
      },

      analyze: function (rawSource) {
        var source = norm(rawSource);
        var file = fromFiles.get(source);
        if (file) {
          return Promise.resolve(
            checked(file.response, source, { kind: 'file', name: file.name }));
        }
        var fixture = bySource.get(source);
        if (!fixture) {
          return Promise.resolve({ status: 'unavailable', message: UNAVAILABLE_HINT });
        }
        var provenance = { kind: 'fixture', id: fixture.id, title: fixture.title };
        if (fixture.response) {
          return Promise.resolve(checked(fixture.response, source, provenance));
        }
        return loadStress().then(function (responses) {
          var response = responses && responses[fixture.id];
          if (!response) {
            return { status: 'unavailable', message: 'Captured result for this example is missing.' };
          }
          return checked(response, source, provenance);
        }, function (err) {
          return { status: 'unavailable',
            message: 'Could not load the large captured results: ' + err.message };
        });
      },

      loadResultText: function (text, name) {
        var parsed;
        try {
          parsed = JSON.parse(text);
        } catch (e) {
          return { status: 'unavailable', message: 'Not valid JSON: ' + e.message };
        }
        var problems = contract.validateResponse(parsed);
        if (problems.length) {
          return { status: 'unavailable',
            message: 'Not a valid analysis result (' + problems[0] + ').' };
        }
        fromFiles.set(norm(parsed.source), { response: parsed, name: name });
        return ok(parsed, { kind: 'file', name: name });
      }
    };
  }

  // ---- Live adapter ---------------------------------------------------------------

  function createHttpAnalyzer(options) {
    options = options || {};
    var baseUrl = options.baseUrl || '';          // '' = same origin as the page
    var timeoutMs = options.timeoutMs || 15000;
    var fetchImpl = options.fetch ||
      (typeof fetch === 'function' ? fetch.bind(root) : null);
    var where = baseUrl || (root.location && root.location.origin) || 'the server';

    function failure(kind, message, httpStatus) {
      var out = { status: 'error', kind: kind, message: message };
      if (httpStatus !== undefined) out.httpStatus = httpStatus;
      return out;
    }

    function networkMessage(err) {
      return 'Could not reach the analysis server at ' + where +
        '. Is it running? Start it with scripts\\run.ps1. (' + (err && err.message || err) + ')';
    }

    // The server's error body is { error: { code, message } }.
    function httpFailureMessage(res, text) {
      try {
        var body = JSON.parse(text);
        if (body && body.error && typeof body.error.message === 'string') {
          return body.error.message + ' (HTTP ' + res.status + ')';
        }
      } catch (e) { /* not JSON */ }
      return 'The server answered HTTP ' + res.status + (res.statusText ? ' ' + res.statusText : '') + '.';
    }

    return {
      id: 'live',
      live: true,
      label: 'Live: C++ compiler via the local server',

      // Liveness probe; resolves { ok, message } and never rejects.
      health: function () {
        if (!fetchImpl) return Promise.resolve({ ok: false, message: 'fetch is not available' });
        return fetchImpl(baseUrl + '/api/health', { cache: 'no-store' }).then(function (res) {
          return res.json().then(function (body) {
            var ok = res.ok && body && body.service === 'ptagen';
            return { ok: !!ok, message: ok ? '' : 'Unexpected answer from the server.' };
          });
        }).catch(function (err) {
          return { ok: false, message: networkMessage(err) };
        });
      },

      analyze: function (source) {
        if (!fetchImpl) {
          return Promise.resolve(failure('network', 'This browser cannot make HTTP requests.'));
        }
        var controller = typeof AbortController !== 'undefined' ? new AbortController() : null;
        var timedOut = false;
        var timer = setTimeout(function () {
          timedOut = true;
          if (controller) controller.abort();
        }, timeoutMs);

        function transportFailure(err) {
          return timedOut
            ? failure('timeout', 'The server did not answer within ' + Math.round(timeoutMs / 1000) + ' seconds.')
            : failure('network', networkMessage(err));
        }

        return fetchImpl(baseUrl + '/api/analyze', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ source: source }),
          cache: 'no-store',
          signal: controller ? controller.signal : undefined
        }).then(function (res) {
          return res.text().then(function (text) {
            if (!res.ok) return failure('http', httpFailureMessage(res, text), res.status);

            var parsed;
            try {
              parsed = JSON.parse(text);
            } catch (e) {
              return failure('invalid-response', 'The server returned something that is not JSON.');
            }
            var problems = contract.validateResponse(parsed);
            if (problems.length) {
              return failure('invalid-response',
                'The server returned a malformed result: ' + problems[0] + '.');
            }
            // Never show a result for text other than what was sent.
            if (norm(parsed.source) !== norm(source)) {
              return failure('invalid-response',
                'The server returned a result for a different source; it was discarded.');
            }
            return { status: 'ok', response: parsed, provenance: { kind: 'live' } };
          }, transportFailure);
        }, transportFailure).then(function (outcome) {
          clearTimeout(timer);
          return outcome;
        });
      }
    };
  }

  // Browser helper: load a script once and resolve when it has run.
  function loadScriptOnce(url) {
    return new Promise(function (resolve, reject) {
      var el = document.createElement('script');
      el.src = url;
      el.onload = function () { resolve(); };
      el.onerror = function () { reject(new Error('failed to load ' + url)); };
      document.head.appendChild(el);
    });
  }

  var api = {
    createFixtureAnalyzer: createFixtureAnalyzer,
    createHttpAnalyzer: createHttpAnalyzer,
    loadScriptOnce: loadScriptOnce,
    UNAVAILABLE_HINT: UNAVAILABLE_HINT
  };

  root.PTAGEN = root.PTAGEN || {};
  root.PTAGEN.analyzer = api;
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
})(typeof window !== 'undefined' ? window : globalThis);
