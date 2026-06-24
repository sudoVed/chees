/* engine-bridge.js — load the WASM engine and wrap its C ABI in a clean JS API.
 * Classic script (no ES modules) so the page works from file:// too.
 * Exposes a global `Engine`. All chess logic lives in WASM; JS only paints. */
(function (global) {
  'use strict';

  // status codes mirror engine/rules/gamestate.h
  const STATUS = {
    0: 'ongoing', 1: 'check', 2: 'checkmate', 3: 'stalemate',
    4: 'draw-50', 5: 'draw-repetition', 6: 'draw-material'
  };

  function b64ToBytes(b64) {
    const bin = atob(b64);
    const len = bin.length;
    const bytes = new Uint8Array(len);
    for (let i = 0; i < len; i++) bytes[i] = bin.charCodeAt(i);
    return bytes;
  }

  const Engine = {
    ready: false,
    _e: null,        // wasm exports

    async load() {
      let bytes;
      if (global.CHESS_WASM_BASE64) {
        bytes = b64ToBytes(global.CHESS_WASM_BASE64);   // embedded (file:// safe)
      } else {
        const resp = await fetch('../dist/engine.wasm?v=2');
        bytes = new Uint8Array(await resp.arrayBuffer());
      }
      const { instance } = await WebAssembly.instantiate(bytes, {});
      this._e = instance.exports;
      this._e.engine_init();
      this.ready = true;
      return this;
    },

    // ---- memory views (re-created lazily; buffer doesn't grow at runtime) ----
    _u8() { return new Uint8Array(this._e.memory.buffer); },
    _i32() { return new Int32Array(this._e.memory.buffer); },

    // ---- lifecycle ----
    newGame() { this._e.new_game(); },

    loadFEN(fen) {
      const ptr = this._e.get_fen_buf();
      const mem = this._u8();
      for (let i = 0; i < fen.length; i++) mem[ptr + i] = fen.charCodeAt(i);
      return this._e.load_fen(fen.length) === 1;
    },

    // ---- board: array[64] of piece codes (0 empty; 1..6 white P..K; 7..12 black) ----
    board() {
      const ptr = this._e.get_board();
      const mem = this._u8();
      const out = new Array(64);
      for (let i = 0; i < 64; i++) out[i] = mem[ptr + i];
      return out;
    },

    sideToMove() { return this._e.side_to_move(); },   // 0 white, 1 black
    inCheck() { return this._e.in_check() === 1; },
    halfmoveClock() { return this._e.halfmove_clock(); },
    fullmoveNumber() { return this._e.fullmove_number(); },

    lastMove() {
      const f = this._e.last_move_from(), t = this._e.last_move_to();
      return f < 0 ? null : { from: f, to: t };
    },

    // ---- THE LIMITER: legal destinations from `square` ----
    legalMoves(square) {
      const n = this._e.get_legal_moves(square);
      const i32 = this._i32();
      const tb = this._e.get_targets_buf() >> 2;
      const fb = this._e.get_flags_buf() >> 2;
      const out = [];
      for (let i = 0; i < n; i++) {
        const flag = i32[fb + i];
        const capture = flag === 4 || flag === 5 || flag >= 12;
        out.push({ to: i32[tb + i], flag, capture });
      }
      return out;
    },

    isCapture(from, to) { return this._e.api_is_capture(from, to) === 1; },
    isPromotion(from, to) { return this._e.api_is_promotion(from, to) === 1; },

    // promo: 0 auto-queen, else PieceType 1..4 (knight,bishop,rook,queen)
    makeMove(from, to, promo) {
      const code = this._e.api_make_move(from, to, promo | 0);
      return code < 0 ? null : STATUS[code];
    },

    undo() { return this._e.undo() === 1; },
    status() { return STATUS[this._e.get_status()]; },

    // ---- search (Phase 9): best move for the side to move ----
    searchBestMove(depth, maxNodes) {
      const m = this._e.search_best_move(depth | 0, (maxNodes | 0));
      if (!m) return null;
      const from = m & 63, to = (m >> 6) & 63, flag = (m >> 12) & 15;
      const promo = (flag & 8) ? ((flag & 3) + 1) : 0;   // 1..4 = N,B,R,Q
      return { from, to, promo,
               score: this._e.search_score() | 0,
               nodes: this._e.search_nodes() | 0,
               depth: this._e.search_depth() | 0 };
    },

    // ---- NNUE: load a net.nnue blob; the AI then evaluates with it ----
    loadNNUE(bytes) {
      console.log(
        "NNUE required:",
        bytes.length,
        "capacity:",
        this._e.nnue_blob_capacity()
      );
      if (bytes.length > this._e.nnue_blob_capacity()) return false;
      this._u8().set(bytes, this._e.get_nnue_buf());
      return this._e.load_nnue(bytes.length) === 1;
    },
    nnueActive() { return this._e.nnue_active() === 1; },

    // Force the search to use NNUE (true) or HCE (false). Used by the file://
    // synchronous fallback; the Web Worker drives its own engine instance.
    setSearchEval(useNnue) { if (this._e.set_search_eval) this._e.set_search_eval(useNnue ? 1 : 0); },

    // Serialize the current position to a FEN (so a Web Worker can be handed it).
    getFEN() {
      const b = this.board();
      const M = {1:'P',2:'N',3:'B',4:'R',5:'Q',6:'K',7:'p',8:'n',9:'b',10:'r',11:'q',12:'k'};
      const rows = [];
      for (let r = 7; r >= 0; r--) {
        let row = '', empty = 0;
        for (let f = 0; f < 8; f++) {
          const c = b[r * 8 + f];
          if (!c) empty++; else { if (empty) { row += empty; empty = 0; } row += M[c]; }
        }
        if (empty) row += empty;
        rows.push(row);
      }
      const side = this.sideToMove() === 0 ? 'w' : 'b';
      const cr = this._e.get_castling();
      let cs = ''; if (cr & 1) cs += 'K'; if (cr & 2) cs += 'Q'; if (cr & 4) cs += 'k'; if (cr & 8) cs += 'q';
      if (!cs) cs = '-';
      const ep = this._e.get_ep();
      const eps = ep < 0 ? '-' : ('abcdefgh'[ep & 7] + (1 + (ep >> 3)));
      return `${rows.join('/')} ${side} ${cs} ${eps} ${this._e.halfmove_clock()} ${this._e.fullmove_number()}`;
    },

    // Fetch a net's bytes from a relative `models/` path (static-hosting safe:
    // GitHub/Cloudflare Pages, or any static server). No backend needed.
    async fetchNet(name) {
      let file = name;
      if (!file) {
        try {
          const r = await fetch('models/manifest.json', { cache: 'no-cache' });
          if (r.ok) { const j = await r.json(); file = j.models && j.models[0] && j.models[0].file; }
        } catch (e) { /* no manifest -> default below */ }
      }
      file = file || 'net.nnue';
      let resp;
      try { resp = await fetch('models/' + file, { cache: 'force-cache' }); }
      catch (e) { return { ok: false, error: 'fetch failed (use a server, not file://)' }; }
      if (!resp.ok) return { ok: false, error: 'model not found (HTTP ' + resp.status + ')' };
      const bytes = new Uint8Array(await resp.arrayBuffer());
      return { ok: true, bytes, name: file, size: bytes.length };
    },

    // Fetch + load a net into THIS (main-thread) instance, for the eval bar.
    async loadNetFromServer(name) {
      const r = await this.fetchNet(name);
      if (!r.ok) return r;

      console.log("NNUE file:", r.name);
      console.log("NNUE size:", r.bytes.length);

      const ok = this.loadNNUE(r.bytes);

      console.log("loadNNUE:", ok);
      console.log("nnueActive:", this.nnueActive());

      return { ok, name: r.name, size: r.size };
    },

    // perft passthrough (handy for debugging in the console)
    perft(depth) { return Number(this._e.perft_current(depth)); }
  };

  global.Engine = Engine;
})(window);
