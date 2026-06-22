/* interaction.js — selection, the legal-move limiter, move execution, and the
 * Play-with-AI flow. A move is accepted ONLY if its target is in
 * Engine.legalMoves(square). */
(function (global) {
  'use strict';

  function pieceColor(code) { return code === 0 ? -1 : (code <= 6 ? 0 : 1); }

  const Interaction = {
    selected: -1,
    legal: [],
    gameOver: false,
    onStateChange: null,
    mode: 'pvp',
    humanColor: 0,
    aiDepth: 5,
    aiTimeMs: 0,
    aiUseNnue: false,
    thinking: false,
    tableMode: false,   // HvH: board fixed, all pieces rotate 180 on Black's turn
    epoch: 0,           // bumped on new game / exit; in-flight AI results from old epochs are ignored
    aiMoveSan: '',      // last AI move in algebraic notation (shown in the status area)
    aiTag: '',          // "via NNUE D10 · 300 ms"
    _aiPending: null,
    deadByColor: { 0: [], 1: [] },   // captured piece TYPES, keyed by the colour that lost them
    captureLog: [],                  // per-ply: {color,type} on a capture else null (for undo)

    setTableMode(on) {
      this.tableMode = !!on;
      try { localStorage.setItem('tableMode', on ? '1' : '0'); } catch (e) {}
      this.refresh();
    },

    // Fully stop play (called on Exit): any pending/in-flight AI move is discarded.
    halt() { this.epoch++; this.thinking = false; this._aiPending = null; },

    init(onStateChange) {
      this.onStateChange = onStateChange;
      this._buildPromoModal();
      this._layout();
      global.addEventListener('resize', () => this._layout());
    },

    reset(mode, humanColor, depth, timeMs, modelName, useNnue) {
      this.epoch++;                       // invalidate any in-flight AI move from a prior game
      this.mode = mode || 'pvp';
      this.humanColor = humanColor || 0;
      this.aiDepth = depth || 5;
      this.aiTimeMs = timeMs || 1000;
      this.aiUseNnue = !!useNnue;
      this.thinking = false;
      this.selected = -1;
      this.legal = [];
      this.gameOver = false;
      this.aiMoveSan = ''; this.aiTag = ''; this._aiPending = null;
      this.deadByColor = { 0: [], 1: [] }; this.captureLog = [];
      const turnEl = document.querySelector('.turn');     // AI mode: no dot, centered gold move readout
      if (turnEl) turnEl.classList.toggle('ai', this.mode === 'pvai');
      this._layout();
      this.refresh();
      this._maybeAI();      // AI moves first if the human chose Black
    },

    handleClick(sq2) {
      if (global.Sound) Sound.unlock();   // first user gesture enables audio
      if (this.gameOver || this.thinking) return;
      const side = Engine.sideToMove();
      if (this.mode === 'pvai' && side !== this.humanColor) return;
      const board = Engine.board();

      if (this.selected === -1) {
        if (pieceColor(board[sq2]) === side) this._select(sq2);
        return;
      }
      const move = this.legal.find(m => m.to === sq2);
      if (move) { this._tryMove(this.selected, sq2); return; }
      if (sq2 === this.selected) { this._deselect(); return; }
      if (pieceColor(board[sq2]) === side) { this._select(sq2); return; }
      this._deselect();
    },

    _select(s) { this.selected = s; this.legal = Engine.legalMoves(s); this._draw(); },
    _deselect() { this.selected = -1; this.legal = []; this._draw(); },

    _tryMove(from, to) {
      if (Engine.isPromotion(from, to)) {
        this._askPromotion(Engine.sideToMove(), (pt) => this._applyMove(from, to, pt));
      } else {
        this._applyMove(from, to, 0);
      }
    },

    // Single path for actually playing a move: detect capture (for sound), make
    // the move in the engine, play the sound, slide the piece, then re-render.
    // Algebraic notation (SAN) for a move, computed from the CURRENT (pre-move)
    // position — needs the board before the move for capture/disambiguation.
    _san(from, to, promo) {
      const b = Engine.board();
      const code = b[from]; if (!code) return '';
      const type = (code - 1) % 6, side = code <= 6 ? 0 : 1;
      const F = (s) => 'abcdefgh'[s & 7], R = (s) => '' + (1 + (s >> 3)), N = (s) => F(s) + R(s);
      if (type === 5 && Math.abs((to & 7) - (from & 7)) === 2) return (to & 7) > (from & 7) ? 'O-O' : 'O-O-O';
      const cap = b[to] !== 0 || (type === 0 && (from & 7) !== (to & 7));   // incl. en passant
      if (type === 0) {
        let s = cap ? F(from) + 'x' + N(to) : N(to);
        if (promo) s += '=' + ['N', 'B', 'R', 'Q'][promo - 1];
        return s;
      }
      let sameFile = false, sameRank = false, ambig = false;
      for (let q = 0; q < 64; q++) {
        if (q === from) continue;
        const c = b[q]; if (!c || ((c - 1) % 6) !== type || (c <= 6 ? 0 : 1) !== side) continue;
        if (Engine.legalMoves(q).some((m) => m.to === to)) {
          ambig = true;
          if ((q & 7) === (from & 7)) sameFile = true;
          if ((q >> 3) === (from >> 3)) sameRank = true;
        }
      }
      let dis = ''; if (ambig) dis = !sameFile ? F(from) : (!sameRank ? R(from) : N(from));
      return ['', 'N', 'B', 'R', 'Q', 'K'][type] + dis + (cap ? 'x' : '') + N(to);
    },

    _commitMove(from, to, promo, afterCb) {
      const pre = Engine.board();
      const moverColor = pre[from] <= 6 ? 0 : 1;
      const moverKing = (pre[from] === 6 || pre[from] === 12);
      const isPawn = ((pre[from] - 1) % 6) === 0;
      const isCap = pre[to] !== 0;                               // normal capture
      const isEnPassant = isPawn && (from & 7) !== (to & 7) && pre[to] === 0;
      const isCastle = moverKing && Math.abs((to & 7) - (from & 7)) === 2;
      // which piece (if any) just died — a genuine capture, NOT a promotion
      let capColor = -1, capType = -1;
      if (isCap) { capColor = pre[to] <= 6 ? 0 : 1; capType = (pre[to] - 1) % 6; }
      else if (isEnPassant) { capColor = 1 - moverColor; capType = 0; }
      const status = Engine.makeMove(from, to, promo);
      if (status === null) { afterCb && afterCb(false); return; }
      if (capType >= 0) { this.deadByColor[capColor].push(capType); this.captureLog.push({ color: capColor, type: capType }); }
      else this.captureLog.push(null);
      const givesCheck = Engine.inCheck();
      if (global.Sound) {
        let kind = 'move';
        if (givesCheck) kind = 'check';
        else if (promo) kind = 'promote';
        else if (isCastle) kind = 'castle';
        else if (isCap) kind = 'capture';
        Sound.play(kind);
      }
      if (this._aiPending) {                                     // an AI move: show SAN + tag
        const suf = (Engine.status() === 'checkmate') ? '#' : (givesCheck ? '+' : '');
        this.aiMoveSan = this._aiPending.san + suf;
        this.aiTag = this._aiPending.tag;
        this._aiPending = null;
      } else {                                                   // a human move: clear the AI display
        this.aiMoveSan = ''; this.aiTag = '';
      }
      BoardUI.animateMove(from, to, () => { this.refresh(); afterCb && afterCb(true); });
    },

    _applyMove(from, to, promo) {
      this.selected = -1; this.legal = [];
      this._commitMove(from, to, promo, (ok) => {
        if (!ok) { this.refresh(); return; }
        this._maybeAI();
      });
    },

    // Undo the last ply (or two, vs AI), keeping the dead-piece trays in sync.
    undo() {
      if (this.thinking || !Engine.undo()) return false;
      this._popCapture();
      if (this.mode === 'pvai' && Engine.sideToMove() !== this.humanColor && Engine.undo()) this._popCapture();
      this.selected = -1; this.legal = []; this.gameOver = false;
      this.aiMoveSan = ''; this.aiTag = '';
      if (global.Sound) Sound.play('move');
      this.refresh();
      return true;
    },
    _popCapture() {
      const e = this.captureLog.pop();
      if (e) { const a = this.deadByColor[e.color], i = a.lastIndexOf(e.type); if (i >= 0) a.splice(i, 1); }
    },

    // Fill the two trays with captured pieces (order Q,R,B,N,P). Tray B is the
    // right/bottom side (the colour currently at the bottom), tray A the other.
    _renderTrays(orientation) {
      const a = document.getElementById('tray-a'), b = document.getElementById('tray-b');
      if (!a || !b) return;
      const ORDER = { 4: 0, 3: 1, 2: 2, 1: 3, 0: 4 };
      const fill = (el, color) => {
        const list = this.deadByColor[color].slice().sort((x, y) => ORDER[x] - ORDER[y]);
        el.innerHTML = list.map((t) =>
          '<span class="dead">' + BoardUI.pieceSVG(t + 1 + (color === 1 ? 6 : 0)) + '</span>').join('');
      };
      // trophy style: each side's tray shows the pieces IT captured (opponent's dead)
      fill(b, 1 - orientation);      // bottom/right player's captures
      fill(a, orientation);          // top/left player's captures
    },

    // Side trays (left+right) when there's spare width; otherwise stack top+bottom.
    _layout() {
      const pa = document.querySelector('.play-area'); if (!pa) return;
      const boardH = Math.min(window.innerHeight - 96, 760);
      const wide = window.innerWidth >= boardH + 150;   // room for the board plus two side trays
      pa.classList.toggle('side', wide);
      pa.classList.toggle('stack', !wide);
    },

    // AI thinks (in a Web Worker if available, else synchronously), logs
    // depth/time/positions, then plays.
    _maybeAI() {
      if (this.mode !== 'pvai' || this.gameOver || this.thinking) return;
      if (Engine.sideToMove() === this.humanColor) return;
      this.thinking = true;
      if (this.onStateChange)
        this.onStateChange({ status: 'thinking', side: Engine.sideToMove(), gameOver: false });
      const depth = this.aiDepth, timeMs = this.aiTimeMs;
      const useNnue = this.aiUseNnue;
      const myEpoch = this.epoch;                         // capture; ignore results if we exit/restart

      // Always wait at least MIN_MS before playing — instant moves look jarring.
      const MIN_MS = 500, thinkStart = performance.now();
      const finish = (from, to, promo, tag) => {
        const wait = Math.max(0, MIN_MS - (performance.now() - thinkStart));
        setTimeout(() => {
          if (this.epoch !== myEpoch) return;            // game exited/restarted — drop this move
          this._aiPending = { san: this._san(from, to, promo), tag };
          this._commitMove(from, to, promo, () => { this.thinking = false; });
        }, wait);
      };
      const bail = () => { if (this.epoch === myEpoch) { this.thinking = false; this.refresh(); } };

      // 1) Opening book: if the exact position is in book, play a book move (for
      // BOTH NNUE and HCE models) instead of searching.
      const bk = global.Book && Book.ready ? Book.probe(Engine.getFEN()) : null;
      if (bk) { finish(bk.from, bk.to, bk.promo, 'Book move'); return; }

      // 2) Otherwise search with the selected eval.
      if (global.AIWorker && AIWorker.available()) {
        const fen = Engine.getFEN();                       // off-thread search
        AIWorker.search(fen, depth, timeMs, useNnue).then((m) => {
          if (this.epoch !== myEpoch) return;
          if (!m || m.from === m.to) { bail(); return; }
          const promo = (m.flag & 8) ? ((m.flag & 3) + 1) : 0;
          const ran = (m.evalMode === 1) ? 'NNUE' : (m.evalMode === 0) ? 'HCE' : (useNnue ? 'NNUE' : 'HCE');
          finish(m.from, m.to, promo, `via ${ran} D${m.depth} · ${m.ms} ms`);
        });
      } else {
        setTimeout(() => {                                 // fallback (e.g. file://)
          if (this.epoch !== myEpoch) return;
          Engine.setSearchEval(useNnue);
          const t0 = performance.now();
          const mv = Engine.searchBestMove(depth, 800000);   // file:// fallback (node cap)
          if (!mv || mv.from === mv.to) { bail(); return; }
          finish(mv.from, mv.to, mv.promo, `via ${useNnue ? 'NNUE' : 'HCE'} D${mv.depth} · ${Math.round(performance.now() - t0)} ms`);
        }, 30);
      }
    },

    refresh() {
      let orientation, pieceFlip = false;
      if (this.mode === 'pvai') {
        orientation = this.humanColor;                 // fixed to the human's side
      } else if (this.tableMode) {
        orientation = 0;                               // board fixed; rotate ALL pieces on Black's turn
        pieceFlip = (Engine.sideToMove() === 1);
      } else {
        orientation = Engine.sideToMove();             // whole board flips each turn
      }
      BoardUI.setPieceFlip(pieceFlip);
      BoardUI.setOrientation(orientation);
      BoardUI.render(Engine.board());
      this._renderTrays(orientation);
      this._draw();
      this._reportStatus();
    },

    _draw() {
      BoardUI.clearHighlights();
      const lm = Engine.lastMove();
      if (lm) BoardUI.setLastMove(lm.from, lm.to);
      if (Engine.inCheck()) {
        const kingCode = Engine.sideToMove() === 0 ? 6 : 12;
        const board = Engine.board();
        for (let s = 0; s < 64; s++) if (board[s] === kingCode) { BoardUI.setCheck(s); break; }
      }
      if (this.selected >= 0) { BoardUI.setSelected(this.selected); BoardUI.setLegal(this.legal); }
    },

    _reportStatus() {
      const status = Engine.status();
      const side = Engine.sideToMove();
      const over = (status === 'checkmate' || status === 'stalemate' || status.startsWith('draw'));
      if (over && !this.gameOver && global.Sound) Sound.play('gameend');   // play once, on game end
      this.gameOver = over;
      if (this.onStateChange) this.onStateChange({
        status, side, gameOver: this.gameOver, aiMoveSan: this.aiMoveSan, aiTag: this.aiTag,
      });
    },

    _buildPromoModal() {
      const modal = document.createElement('div');
      modal.id = 'promo-modal'; modal.className = 'modal hidden';
      modal.innerHTML = '<div class="modal-card"><h3>Promote to</h3><div class="promo-choices"></div></div>';
      document.body.appendChild(modal);
      this._promoModal = modal;
    },

    _askPromotion(side, cb) {
      const modal = this._promoModal;
      const choices = modal.querySelector('.promo-choices');
      choices.innerHTML = '';
      for (const [pt, name] of [[4, 'Queen'], [3, 'Rook'], [2, 'Bishop'], [1, 'Knight']]) {
        const code = pt + 1 + (side === 1 ? 6 : 0);
        const btn = document.createElement('button');
        btn.className = 'promo-btn'; btn.title = name;
        btn.innerHTML = BoardUI.pieceSVG(code);
        btn.addEventListener('click', () => { modal.classList.add('hidden'); cb(pt); });
        choices.appendChild(btn);
      }
      modal.classList.remove('hidden');
    }
  };

  global.Interaction = Interaction;
})(window);
