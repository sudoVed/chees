/* main.js — boot the engine, wire the UI, render the turn indicator + result. */
(function () {
  'use strict';

  const COLOR_NAME = ['White', 'Black'];

  function statusMessage(state) {
    const { status, side, aiMoveSan, aiTag } = state;
    const mover = COLOR_NAME[side];
    switch (status) {
      case 'checkmate': return { text: COLOR_NAME[side ^ 1] + ' wins by checkmate', tag: '', over: true };
      case 'stalemate': return { text: 'Draw — stalemate', tag: '', over: true };
      case 'draw-50': return { text: 'Draw — 50-move rule', tag: '', over: true };
      case 'draw-repetition': return { text: 'Draw — threefold repetition', tag: '', over: true };
      case 'draw-material': return { text: 'Draw — insufficient material', tag: '', over: true };
      case 'thinking': return { text: 'Thinking…', tag: '', over: false };
    }
    // ongoing: if the AI just moved, show its move (SAN) + the eval tag here
    if (aiMoveSan) return { text: aiMoveSan, tag: aiTag || '', over: false };
    if (status === 'check') return { text: mover + ' to move — Check', tag: '', over: false };
    return { text: mover + ' to move', tag: '', over: false };
  }

  function onStateChange(state) {
    const turn = document.querySelector('.turn');
    const text = document.getElementById('status-text');
    const tagEl = document.getElementById('move-tag');
    const dot = document.getElementById('turn-dot');
    const banner = document.getElementById('result-banner');
    const resultText = document.getElementById('result-text');

    const msg = statusMessage(state);
    text.textContent = msg.text;
    if (tagEl) tagEl.textContent = msg.tag;
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
