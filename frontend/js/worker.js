/* worker.js — runs a SEPARATE engine instance in a Web Worker so the search
 * never blocks the UI, with a wall-clock TIME budget (iterative deepening:
 * deepen until the time limit, keep the best move from the last finished depth). */
importScripts('engine-wasm.js');   // sets globalThis.CHESS_WASM_BASE64

let E = null;
let netLoaded = false;   // did a net successfully load into THIS worker's engine?
function b64(b) { const s = atob(b), a = new Uint8Array(s.length); for (let i = 0; i < s.length; i++) a[i] = s.charCodeAt(i); return a; }

const ready = (async () => {
  const { instance } = await WebAssembly.instantiate(b64(globalThis.CHESS_WASM_BASE64), {});
  E = instance.exports; E.engine_init();
})();

function loadFEN(fen) {
  const ptr = E.get_fen_buf(), mem = new Uint8Array(E.memory.buffer);
  for (let i = 0; i < fen.length; i++) mem[ptr + i] = fen.charCodeAt(i);
  return E.load_fen(fen.length) === 1;
}

self.onmessage = async (ev) => {
  await ready;
  const msg = ev.data;
  if (msg.type === 'loadnet') {
    new Uint8Array(E.memory.buffer).set(msg.bytes, E.get_nnue_buf());
    netLoaded = E.load_nnue(msg.bytes.length) === 1;
    self.postMessage({ type: 'netloaded', ok: netLoaded });
  } else if (msg.type === 'search') {
    // NNUE-only or HCE-only — the two never mix. NNUE needs a loaded net.
    const useNnue = !!msg.useNnue && netLoaded;
    if (E.set_search_eval) E.set_search_eval(useNnue ? 1 : 0);
    loadFEN(msg.fen);
    const start = performance.now();
    const budget = Math.max(50, msg.timeMs || 1000);
    const maxDepth = Math.max(1, msg.depth || 64);
    let best = 0, reached = 0, nodes = 0, nps = 300;   // nodes/ms estimate
    for (let d = 1; d <= maxDepth; d++) {
      const remaining = budget - (performance.now() - start);
      if (remaining <= 5) break;
      // node cap sized to the time left, so this depth aborts cleanly if it would
      // overshoot (search returns the best move from the last completed depth).
      const cap = Math.max(40000, Math.floor(remaining * nps));
      const cs = performance.now();
      const m = E.search_best_move(d, cap);
      const cms = performance.now() - cs;
      const dd = E.search_depth() | 0;
      if (m) { best = m; reached = dd; nodes = E.search_nodes() | 0; }
      if (cms > 1) nps = Math.max(50, (E.search_nodes() | 0) / cms);
      if (dd < d) break;                       // aborted by the cap -> out of time
      if (performance.now() - start >= budget) break;
    }
    self.postMessage({
      type: 'bestmove', reqId: msg.reqId,
      from: best & 63, to: (best >> 6) & 63, flag: (best >> 12) & 15,
      score: E.search_score() | 0, depth: reached, nodes, ms: Math.round(performance.now() - start),
      evalMode: useNnue ? 1 : 0   // which eval ACTUALLY ran: 1=NNUE 0=HCE
    });
  }
};
