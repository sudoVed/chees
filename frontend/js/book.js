/* book.js — Polyglot opening-book probe (pure JS, main thread).
 *
 * Hashes the current position to a Polyglot key (using the 781 standard
 * constants in polyglot-keys.js), binary-searches Book.bin for that exact key,
 * and returns a weighted-random book move — or null if the position isn't in
 * book. The engine then plays the book move; otherwise search (NNUE/HCE) runs.
 *
 * Book.bin format: 16-byte big-endian entries { key:u64, move:u16, weight:u16,
 * learn:u32 }, sorted by key. Move bits: to(0-5) from(6-11) promo(12-14).
 */
(function (global) {
  'use strict';

  const K = global.POLYGLOT_KEYS, OFF = global.POLYGLOT_OFF;

  // piece char -> Polyglot "kind" (black pawn=0, white pawn=1, ... white king=11)
  const KIND = { p:0, P:1, n:2, N:3, b:4, B:5, r:6, R:7, q:8, Q:9, k:10, K:11 };

  // Parse a FEN into { board[64] (char or 0; sq 0=a1..63=h8), white, castle{K,Q,k,q}, ep }
  function parseFEN(fen) {
    const parts = fen.trim().split(/\s+/);
    const rows = parts[0].split('/');           // rank 8 first
    const board = new Array(64).fill(0);
    for (let r = 0; r < 8; r++) {
      let file = 0;
      for (const ch of rows[r]) {
        if (ch >= '1' && ch <= '8') { file += +ch; }
        else { const sq = (7 - r) * 8 + file; board[sq] = ch; file++; }
      }
    }
    const white = parts[1] === 'w';
    const cr = parts[2] || '-';
    const castle = { K: cr.includes('K'), Q: cr.includes('Q'), k: cr.includes('k'), q: cr.includes('q') };
    const ep = (parts[3] && parts[3] !== '-')
      ? ('abcdefgh'.indexOf(parts[3][0]) + (parseInt(parts[3][1], 10) - 1) * 8) : -1;
    return { board, white, castle, ep };
  }

  // Polyglot Zobrist hash of a position (BigInt).
  function polyglotKey(pos) {
    let h = 0n;
    for (let sq = 0; sq < 64; sq++) {
      const pc = pos.board[sq];
      if (!pc) continue;
      const file = sq & 7, row = sq >> 3;       // row 0 = rank 1
      h ^= K[OFF.PIECE + 64 * KIND[pc] + 8 * row + file];
    }
    if (pos.castle.K) h ^= K[OFF.CASTLE + 0];
    if (pos.castle.Q) h ^= K[OFF.CASTLE + 1];
    if (pos.castle.k) h ^= K[OFF.CASTLE + 2];
    if (pos.castle.q) h ^= K[OFF.CASTLE + 3];
    // En passant: only if a pawn of the side to move can actually capture.
    if (pos.ep >= 0) {
      const epFile = pos.ep & 7;
      let canCapture = false;
      if (pos.white) {                          // white pawn on rank 5 adjacent to ep file
        for (const df of [-1, 1]) { const f = epFile + df; if (f >= 0 && f < 8 && pos.board[4 * 8 + f] === 'P') canCapture = true; }
      } else {                                  // black pawn on rank 4 adjacent
        for (const df of [-1, 1]) { const f = epFile + df; if (f >= 0 && f < 8 && pos.board[3 * 8 + f] === 'p') canCapture = true; }
      }
      if (canCapture) h ^= K[OFF.EP + epFile];
    }
    if (pos.white) h ^= K[OFF.TURN];
    return BigInt.asUintN(64, h);
  }

  const Book = {
    view: null,        // DataView over Book.bin
    count: 0,
    ready: false,

    async load(url) {
      if (this.ready) return true;
      try {
        const r = await fetch(url || 'book/Book.bin', { cache: 'force-cache' });
        if (!r.ok) return false;
        const buf = await r.arrayBuffer();
        this.view = new DataView(buf);
        this.count = (buf.byteLength / 16) | 0;
        this.ready = this.count > 0;
        return this.ready;
      } catch (e) { return false; }
    },

    _keyAt(i) { return this.view.getBigUint64(i * 16, false); },

    // Return { from, to, promo, uci, name } for the current FEN, or null.
    probe(fen) {
      if (!this.ready) return null;
      const pos = parseFEN(fen);
      const key = polyglotKey(pos);

      // binary search for ANY entry with this key
      let lo = 0, hi = this.count - 1, hit = -1;
      while (lo <= hi) {
        const mid = (lo + hi) >> 1, k = this._keyAt(mid);
        if (k < key) lo = mid + 1;
        else if (k > key) hi = mid - 1;
        else { hit = mid; hi = mid - 1; }       // found; keep going left for the first
      }
      if (hit < 0) return null;

      // gather all consecutive entries sharing the key
      const entries = [];
      for (let i = hit; i < this.count && this._keyAt(i) === key; i++) {
        const mv = this.view.getUint16(i * 16 + 8, false);
        const wt = this.view.getUint16(i * 16 + 10, false);
        entries.push({ mv, wt: wt || 1 });
      }
      if (!entries.length) return null;

      // weighted-random pick
      let total = 0; for (const e of entries) total += e.wt;
      let pick = Math.floor(Math.random() * total), chosen = entries[0];
      for (const e of entries) { if (pick < e.wt) { chosen = e; break; } pick -= e.wt; }

      return this._decode(chosen.mv, pos);
    },

    _decode(mv, pos) {
      const toFile = mv & 7, toRow = (mv >> 3) & 7;
      const fromFile = (mv >> 6) & 7, fromRow = (mv >> 9) & 7;
      const promo = (mv >> 12) & 7;             // 0 none, 1..4 = N,B,R,Q (engine match)
      let from = fromRow * 8 + fromFile, to = toRow * 8 + toFile;

      // Polyglot encodes castling as king "to" its own rook (e1->h1 / e1->a1).
      // Remap to the engine's two-square king move (e1g1 / e1c1).
      const pc = pos.board[from];
      if ((pc === 'K' || pc === 'k') && Math.abs(toFile - fromFile) >= 2)
        to = (toFile > fromFile) ? from + 2 : from - 2;

      const sq = s => 'abcdefgh'[s & 7] + (1 + (s >> 3));
      const uci = sq(from) + sq(to) + (promo ? 'nbrq'[promo - 1] : '');
      return { from, to, promo, uci, name: null };
    }
  };

  global.Book = Book;
  global.Book._polyglotKey = (fen) => polyglotKey(parseFEN(fen));   // exposed for tests
})(typeof self !== 'undefined' ? self : this);
