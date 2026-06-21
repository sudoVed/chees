/* main.js — boot the engine, wire the UI, render the turn indicator + result. */
(function () {
  'use strict';

  const COLOR_NAME = ['White', 'Black'];

  function statusMessage({ status, side }) {
    const mover = COLOR_NAME[side];
    switch (status) {
      case 'checkmate': return { text: COLOR_NAME[side ^ 1] + ' wins by checkmate', over: true };
      case 'stalemate': return { text: 'Draw — stalemate', over: true };
      case 'draw-50': return { text: 'Draw — 50-move rule', over: true };
      case 'draw-repetition': return { text: 'Draw — threefold repetition', over: true };
      case 'draw-material': return { text: 'Draw — insufficient material', over: true };
      case 'thinking': return { text: 'AI thinking…', over: false };
      case 'check': return { text: mover + ' to move — Check', over: false };
      default: return { text: mover + ' to move', over: false };
    }
  }

  function onStateChange(state) {
    const turn = document.querySelector('.turn');
    const text = document.getElementById('status-text');
    const dot = document.getElementById('turn-dot');
    const banner = document.getElementById('result-banner');
    const resultText = document.getElementById('result-text');

    const msg = statusMessage(state);
    text.textContent = msg.text;
    dot.className = 'turn-dot ' + (state.side === 0 ? 'white' : 'black');
    turn.classList.toggle('check', state.status === 'check');

    if (state.gameOver) {
      resultText.textContent = msg.text;
      banner.classList.remove('hidden');
    } else {
      banner.classList.add('hidden');
    }
  }

  async function boot() {
    const loading = document.getElementById('loading');
    try {
      await Engine.load();
    } catch (e) {
      loading.textContent = 'Failed to load engine: ' + e.message;
      return;
    }

    BoardUI.init(document.getElementById('board'), (sq) => Interaction.handleClick(sq));
    if (window.AIWorker) AIWorker.init();
    Interaction.init(onStateChange);
    Controls.init();


    loading.classList.add('hidden');
    window.Game = { Engine, BoardUI, Interaction, Controls };
  }

  if (document.readyState === 'loading')
    document.addEventListener('DOMContentLoaded', boot);
  else boot();
})();
