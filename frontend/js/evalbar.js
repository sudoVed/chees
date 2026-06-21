/* evalbar.js — Stockfish-style vertical evaluation bar.
 *
 * Shows which side stands better and by how much, from the static engine eval.
 * The bar is split into a white zone and a black zone; the split position is
 * the side's expected score (a sigmoid of the centipawn eval), so it saturates
 * smoothly near the extremes instead of slamming to the end on a 1-pawn edge.
 *
 * Display units: pawns (centipawns / 100), one decimal — so a queen up reads
 * ~ +9, a clean game stays within roughly +-10. The raw engine eval stays in
 * centipawns internally (the search will need that); only the display is scaled.
 *
 * Flipping: the white zone always sits on the side where White's pieces are.
 * When the board flips to Black's perspective, the bar flips with it, and the
 * score label moves to the leading side's end with contrasting text.
 */
(function (global) {
  'use strict';

  // Map a centipawn eval to White's expected score in [0,1].
  // Logistic with a 400cp scale: +400cp -> ~0.91, -400cp -> ~0.09.
  function winProb(cp) { return 1 / (1 + Math.pow(10, -cp / 400)); }

  const EvalBar = {
    el: null, fillEl: null, scoreEl: null,

    init() {
      this.el = document.getElementById('evalbar');
      this.fillEl = this.el ? this.el.querySelector('.evalbar-fill') : null;
      this.scoreEl = this.el ? this.el.querySelector('.evalbar-score') : null;
    },

    // orientation: 0 = White at bottom, 1 = Black at bottom (board flipped).
    // useNnue: show the net's eval (true) or the hand-crafted eval (false), so the
    // bar matches the AI the user picked. Defaults to NNUE (with HCE fallback).
    update(orientation, useNnue) {
      if (!this.el || !global.Engine || !Engine.ready) return;

      const cp = (useNnue === false) ? Engine.evalWhite() : Engine.evalNnueWhite();   // + = White better
      const f = winProb(cp);                  // White's share of the bar

      // White zone size; flip anchors it to White's side of the board.
      const whiteAtBottom = orientation === 0;
      this.el.classList.toggle('flip', !whiteAtBottom);
      this.fillEl.style.height = (f * 100).toFixed(1) + '%';

      // Score label: magnitude in pawns at the leading side's end.
      const pawns = Math.abs(cp) / 100;
      let txt;
      if (Math.abs(cp) < 5) txt = '0.0';
      else if (pawns >= 100) txt = Math.round(pawns).toString();
      else txt = pawns.toFixed(1);
      this.scoreEl.textContent = txt;

      const leaderWhite = cp >= 0;
      const leaderAtBottom = (leaderWhite === whiteAtBottom);
      this.scoreEl.style.bottom = leaderAtBottom ? '4px' : 'auto';
      this.scoreEl.style.top    = leaderAtBottom ? 'auto' : '4px';
      // dark text on the light (white) zone, light text on the dark (black) zone
      this.scoreEl.style.color = leaderWhite ? '#21232a' : '#f1f1f4';
    }
  };

  global.EvalBar = EvalBar;
})(window);
