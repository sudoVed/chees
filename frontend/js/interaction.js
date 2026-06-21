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

    init(onStateChange) {
      this.onStateChange = onStateChange;
      this._buildPromoModal();
    },

    reset(mode, humanColor, depth, timeMs, modelName, useNnue) {
      this.mode = mode || 'pvp';
      this.humanColor = humanColor || 0;
      this.aiDepth = depth || 5;
      this.aiTimeMs = timeMs || 1000;
      this.aiUseNnue = !!useNnue;
      this.thinking = false;
      this.selected = -1;
      this.legal = [];
      this.gameOver = false;
      this.refresh();
      this._maybeAI();      // AI moves first if the human chose Black
    },

    handleClick(sq2) {
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

    _applyMove(from, to, promo) {
      const status = Engine.makeMove(from, to, promo);
      if (status === null) { this._deselect(); return; }
      this.selected = -1; this.legal = [];
      this.refresh();
      this._maybeAI();
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

      const setLog = (txt) => { const el = document.getElementById('ai-log'); if (el) el.textContent = txt; console.log(txt); };

      const apply = (mv, ms) => {
        if (mv && mv.from !== mv.to) {
          const ran = (mv.evalMode === 1) ? 'NNUE' : (mv.evalMode === 0) ? 'HCE' : (useNnue ? 'NNUE' : 'HCE');
          setLog(`via ${ran} D${mv.depth}  ·  ${ms} ms`);
          Engine.makeMove(mv.from, mv.to, mv.promo);
        }
        this.thinking = false;
        this.refresh();
      };

      // 1) Opening book: if the exact position is in book, play a book move (for
      // BOTH NNUE and HCE models) instead of searching.
      const bk = global.Book && Book.ready ? Book.probe(Engine.getFEN()) : null;
      if (bk && Engine.makeMove(bk.from, bk.to, bk.promo) !== null) {
        setLog(bk.name || 'Book move');
        this.thinking = false;
        this.refresh();
        return;
      }

      // 2) Otherwise search with the selected eval.
      if (global.AIWorker && AIWorker.available()) {
        const fen = Engine.getFEN();                       // off-thread search
        AIWorker.search(fen, depth, timeMs, useNnue).then((m) => apply({
          from: m.from, to: m.to, promo: (m.flag & 8) ? ((m.flag & 3) + 1) : 0,
          depth: m.depth, evalMode: m.evalMode
        }, m.ms));
      } else {
        setTimeout(() => {                                 // fallback (e.g. file://)
          Engine.setSearchEval(useNnue);
          const t0 = performance.now();
          const mv = Engine.searchBestMove(depth, 800000);   // file:// fallback (node cap)
          apply(mv, Math.round(performance.now() - t0));
        }, 30);
      }
    },

    refresh() {
      const orientation = (this.mode === 'pvai') ? this.humanColor : Engine.sideToMove();
      BoardUI.setOrientation(orientation);
      BoardUI.render(Engine.board());
      this._draw();
      // HCE models show the hand-crafted eval; NNUE models show the net's eval.
      const useNnue = (this.mode === 'pvai') ? this.aiUseNnue : true;
      if (global.EvalBar) EvalBar.update(orientation, useNnue);
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
      this.gameOver = (status === 'checkmate' || status === 'stalemate' || status.startsWith('draw'));
      if (this.onStateChange) this.onStateChange({ status, side, gameOver: this.gameOver });
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
