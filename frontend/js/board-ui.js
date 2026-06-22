/* board-ui.js — render the 8x8 board + pieces and manage visual highlights.
 * Pure presentation: it never decides legality, it only draws what it's told. */
(function (global) {
  'use strict';

  const GLYPH = { 0: '♟', 1: '♞', 2: '♝', 3: '♜', 4: '♛', 5: '♚' };

  function pieceSVG(code) {
    const type = (code - 1) % 6;
    const white = code <= 6;
    const fill = white ? '#f6f2e9' : '#161617';
    const stroke = white ? '#1a1a1c' : '#d9cfa8';   /* ivory-gold rim so black pieces read on ebony */
    return (
      '<svg class="piece-svg" viewBox="0 0 100 100" aria-hidden="true">' +
      '<text x="50" y="54" text-anchor="middle" dominant-baseline="central" ' +
      'font-size="78" fill="' + fill + '" stroke="' + stroke +
      '" stroke-width="2.2" paint-order="stroke">' + GLYPH[type] + '</text></svg>'
    );
  }

  const BoardUI = {
    boardEl: null,
    cells: new Array(64),
    orientation: 0,
    pieceFlip: false,     // table mode: rotate black pieces 180 (board stays fixed)
    _built: false,
    onClick: null,

    setPieceFlip(on) { this.pieceFlip = !!on; },

    init(boardEl, onClick) {
      this.boardEl = boardEl;
      this.onClick = onClick;
      this._built = true;
      this._build();
    },

    setOrientation(color) {
      if (this._built && this.orientation === color) return;
      this.orientation = color;
      this._built = true;
      this._build();
    },

    _build() {
      const b = this.boardEl;
      b.innerHTML = '';
      for (let r = 0; r < 8; r++) {
        for (let c = 0; c < 8; c++) {
          const rank = this.orientation === 0 ? 7 - r : r;
          const file = this.orientation === 0 ? c : 7 - c;
          const sq = rank * 8 + file;
          const cell = document.createElement('div');
          cell.className = 'sq ' + (((file + rank) % 2 === 0) ? 'dark' : 'light');
          cell.dataset.sq = sq;

          if (c === 0) {
            const rl = document.createElement('span');
            rl.className = 'coord rank';
            rl.textContent = (rank + 1);
            cell.appendChild(rl);
          }
          if (r === 7) {
            const fl = document.createElement('span');
            fl.className = 'coord file';
            fl.textContent = 'abcdefgh'[file];
            cell.appendChild(fl);
          }

          const marker = document.createElement('div');
          marker.className = 'marker';
          cell.appendChild(marker);

          const pieceLayer = document.createElement('div');
          pieceLayer.className = 'piece';
          cell.appendChild(pieceLayer);

          cell.addEventListener('click', () => this.onClick && this.onClick(sq));
          this.cells[sq] = cell;
          b.appendChild(cell);
        }
      }
    },

    render(boardArr) {
      for (let sq = 0; sq < 64; sq++) {
        const layer = this.cells[sq].querySelector('.piece');
        const code = boardArr[sq];
        const want = code ? String(code) : '';
        if (layer.dataset.code !== want) {
          layer.dataset.code = want;
          layer.innerHTML = code ? pieceSVG(code) : '';
        }
        layer.classList.toggle('flip180', this.pieceFlip && code !== 0);   // table mode: rotate all pieces 180
      }
    },

    // Slide the piece currently on `fromSq` to `toSq`, then call cb(). Uses the
    // live cell rectangles so it's correct regardless of board orientation/size.
    // The caller re-renders the board in cb (which snaps to the final state).
    animateMove(fromSq, toSq, cb) {
      const fromCell = this.cells[fromSq], toCell = this.cells[toSq];
      if (!fromCell || !toCell) { cb && cb(); return; }
      const layer = fromCell.querySelector('.piece');
      const svg = layer && layer.firstElementChild;
      if (!svg) { cb && cb(); return; }
      const a = fromCell.getBoundingClientRect(), b = toCell.getBoundingClientRect();
      const dx = Math.round(b.left - a.left), dy = Math.round(b.top - a.top);
      if (dx === 0 && dy === 0) { cb && cb(); return; }
      let done = false;
      const finish = () => {
        if (done) return; done = true;
        svg.removeEventListener('transitionend', finish);
        layer.style.zIndex = ''; svg.style.transition = ''; svg.style.transform = '';
        cb && cb();
      };
      const rot = layer.classList.contains('flip180') ? ' rotate(180deg)' : '';   // keep table-mode rotation
      layer.style.zIndex = '5';
      svg.style.transition = 'transform 0.2s ease-out';
      svg.addEventListener('transitionend', finish);
      requestAnimationFrame(() => { svg.style.transform = 'translate(' + dx + 'px,' + dy + 'px)' + rot; });
      setTimeout(finish, 320);   // safety net if transitionend doesn't fire
    },

    clearHighlights() {
      for (let sq = 0; sq < 64; sq++)
        this.cells[sq].className = this.cells[sq].className
          .replace(/\b(sel|legal|legal-cap|lastmove|check)\b/g, '').replace(/\s+/g, ' ').trim();
    },

    addClass(sq, cls) { if (sq >= 0) this.cells[sq].classList.add(cls); },
    setSelected(sq) { this.addClass(sq, 'sel'); },
    setLegal(list) {
      for (const m of list) this.addClass(m.to, m.capture ? 'legal-cap' : 'legal');
    },
    setLastMove(from, to) { this.addClass(from, 'lastmove'); this.addClass(to, 'lastmove'); },
    setCheck(sq) { this.addClass(sq, 'check'); }
  };

  global.BoardUI = BoardUI;
  global.BoardUI.pieceSVG = pieceSVG;
})(window);
