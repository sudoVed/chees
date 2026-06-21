/* aiworker.js — main-thread client for the search Web Worker. Falls back to a
 * synchronous search if Workers aren't available (e.g. opened via file://). */
(function (global) {
  'use strict';
  const AIWorker = {
    worker: null, _reqId: 0, _pending: new Map(),

    init() {
      try { this.worker = new Worker('js/worker.js'); }
      catch (e) { this.worker = null; return false; }
      this.worker.onmessage = (ev) => {
        const m = ev.data;
        if (m.type === 'bestmove') {
          const cb = this._pending.get(m.reqId);
          if (cb) { this._pending.delete(m.reqId); cb(m); }
        }
      };
      return true;
    },

    available() { return !!this.worker; },

    loadNet(bytes) {
      if (!this.worker) return Promise.resolve(false);
      return new Promise((res) => {
        const h = (ev) => { if (ev.data.type === 'netloaded') { this.worker.removeEventListener('message', h); res(ev.data.ok); } };
        this.worker.addEventListener('message', h);
        this.worker.postMessage({ type: 'loadnet', bytes: bytes.slice() });   // copy, main keeps its own
      });
    },

    // resolves with { from, to, flag, score, depth, nodes, ms }
    search(fen, depth, timeMs, useNnue) {
      return new Promise((res) => {
        const reqId = ++this._reqId;
        this._pending.set(reqId, res);
        this.worker.postMessage({ type: 'search', fen, depth, timeMs, useNnue: !!useNnue, reqId });
      });
    }
  };
  global.AIWorker = AIWorker;
})(window);
