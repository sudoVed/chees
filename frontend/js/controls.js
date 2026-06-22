/* controls.js — screen flow + top-bar actions.
 * Human vs Human, or Play with AI (pick model + side; model = depth preset). */
(function (global) {
  'use strict';

  // ===========================================================================
  // AI MODELS — the selectable opponents (add/remove freely). EDIT HERE (one place):
  //   eval   : 'nnue' (neural net) or 'hce' (hand-crafted). They never mix —
  //            an 'nnue' model uses only the net, an 'hce' model only the HCE.
  //   depth  : max search depth (iterative deepening stops here or on timeMs).
  //   timeMs : wall-clock budget per move (ms); the search returns the best move
  //            from the last fully-finished depth if it would overshoot.
  //   label  : text shown in the dropdown.
  //   desc   : one-line hint shown under the dropdown.
  // The dropdown <option>s are generated from this object, so labels/order live
  // here only (not in index.html). The net file path is in models/manifest.json
  // (falls back to models/net.nnue) — see engine-bridge.js fetchNet().
  // ===========================================================================
  // Labels are display-only flavour; the real spec (eval + depth) lives in `desc`,
  // so the dropdown reads as a polished difficulty ladder, not a meme.
  const MODELS = {
    AI1: { label: 'Neurally Oblivious',    eval: 'nnue', depth: 3,  timeMs: 1500, desc: 'Neural-net eval · depth 3' },
    AI2: { label: 'Neurally Obvious',      eval: 'nnue', depth: 5,  timeMs: 1500, desc: 'Neural-net eval · depth 5' },
    AI3: { label: 'Neurally Ostentatious', eval: 'nnue', depth: 10, timeMs: 4000, desc: 'Neural-net eval · depth 10' },
    AI4: { label: 'Neurally Omniscient',   eval: 'nnue', depth: 20, timeMs: 5000, desc: 'Neural-net eval · thinks ~5s/move' },
    AI5: { label: 'Classically Hopeless',  eval: 'hce',  depth: 3,  timeMs: 1500, desc: 'Hand-crafted eval · depth 3' },
    AI6: { label: 'Classically Handy',     eval: 'hce',  depth: 5,  timeMs: 1500, desc: 'Hand-crafted eval · depth 5' },
    AI7: { label: 'Classically Heroic',    eval: 'hce',  depth: 10, timeMs: 4000, desc: 'Hand-crafted eval · depth 10' },
    AI8: { label: 'Classically Herculean', eval: 'hce',  depth: 20, timeMs: 5000, desc: 'Hand-crafted eval · thinks ~5s/move' },
  };

  // Inline icons for the audio toggles (on / muted variants).
  const IC = {
    musicOn:  '<svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M9 18V5l12-2v13"/><circle cx="6" cy="18" r="3"/><circle cx="18" cy="16" r="3"/></svg>',
    musicOff: '<svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M9 18V5l12-2v13"/><circle cx="6" cy="18" r="3"/><circle cx="18" cy="16" r="3"/><line x1="3" y1="3" x2="21" y2="21"/></svg>',
    sfxOn:    '<svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M11 5 6 9H2v6h4l5 4z"/><path d="M15.5 8.5a5 5 0 0 1 0 7"/><path d="M19 5a9 9 0 0 1 0 14"/></svg>',
    sfxOff:   '<svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M11 5 6 9H2v6h4l5 4z"/><line x1="23" y1="9" x2="17" y2="15"/><line x1="17" y1="9" x2="23" y2="15"/></svg>',
    rotateOn: '<svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 2v6h-6"/><path d="M3 12a9 9 0 0 1 15-6.7L21 8"/><path d="M3 22v-6h6"/><path d="M21 12a9 9 0 0 1-15 6.7L3 16"/></svg>',
    rotateOff:'<svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M21 2v6h-6"/><path d="M3 12a9 9 0 0 1 15-6.7L21 8"/><path d="M3 22v-6h6"/><path d="M21 12a9 9 0 0 1-15 6.7L3 16"/><line x1="2" y1="2" x2="22" y2="22"/></svg>',
  };

  const Controls = {
    started: false,
    aiSide: 0,           // human side in pvai (0 white, 1 black)

    init() {
      this.startScreen = document.getElementById('start-screen');
      this.gameScreen  = document.getElementById('game-screen');
      this.banner      = document.getElementById('result-banner');
      this.modeButtons = document.getElementById('mode-buttons');
      this.aiSetup     = document.getElementById('ai-setup');
      this.aiStatus    = document.getElementById('ai-status');

      // Build the model dropdown from MODELS so labels/order live in one place.
      const sel = document.getElementById('ai-model');
      const descEl = document.getElementById('ai-model-desc');
      if (sel) {
        sel.innerHTML = '';
        for (const key of Object.keys(MODELS)) {
          const o = document.createElement('option');
          o.value = key; o.textContent = MODELS[key].label;
          sel.appendChild(o);
        }
        const upd = () => { if (descEl) descEl.textContent = (MODELS[sel.value] || {}).desc || ''; };
        sel.addEventListener('change', upd); upd();
      }

      document.getElementById('btn-pvp').addEventListener('click', () => this.startGame('pvp'));
      document.getElementById('btn-pvai').addEventListener('click', () => this.openAISetup());
      document.getElementById('btn-ai-back').addEventListener('click', () => this.closeAISetup());
      document.getElementById('btn-ai-start').addEventListener('click', () => this.startAI());

      for (const b of this.aiSetup.querySelectorAll('.side-btn')) {
        b.addEventListener('click', () => {
          for (const x of this.aiSetup.querySelectorAll('.side-btn')) x.classList.remove('selected');
          b.classList.add('selected');
          this.aiSide = parseInt(b.dataset.side, 10);
        });
      }

      document.getElementById('btn-undo').addEventListener('click', () => {
        if (this.started) Interaction.undo();
      });
      document.getElementById('btn-exit').addEventListener('click', () => this.toStart());
      document.getElementById('btn-result-new').addEventListener('click', () => this.toStart());
      // Dismiss the end-game banner (X) so the final board is visible.
      document.getElementById('btn-result-close').addEventListener('click', () => this.banner.classList.add('hidden'));

      // Unlock audio on the FIRST user interaction anywhere — synchronously, so it
      // still counts as a user gesture (calling unlock() after an `await`, e.g. the
      // net fetch on "Start game", is too late and the browser blocks playback).
      const unlockAudio = () => { if (global.Sound) Sound.unlock(); };
      window.addEventListener('pointerdown', unlockAudio, { once: true });
      window.addEventListener('keydown', unlockAudio, { once: true });

      // ---- audio toggles (music + sfx) — present on BOTH the home and game screens ----
      const musicBtns = document.querySelectorAll('.js-music');
      const sfxBtns   = document.querySelectorAll('.js-sfx');
      const syncAudio = () => {
        if (!global.Sound) return;
        musicBtns.forEach((b) => { b.innerHTML = Sound.music   ? IC.musicOn : IC.musicOff; b.classList.toggle('off', !Sound.music); });
        sfxBtns.forEach((b)   => { b.innerHTML = Sound.effects ? IC.sfxOn   : IC.sfxOff;   b.classList.toggle('off', !Sound.effects); });
      };
      syncAudio();
      musicBtns.forEach((b) => b.addEventListener('click', () => { if (global.Sound) { Sound.unlock(); Sound.toggleMusic(); } syncAudio(); }));
      sfxBtns.forEach((b)   => b.addEventListener('click', () => { if (global.Sound) { Sound.unlock(); Sound.toggleEffects(); if (Sound.effects) Sound.hover(); } syncAudio(); }));

      // ---- board-rotation toggle (Human-vs-Human only) ----
      try { Interaction.tableMode = localStorage.getItem('tableMode') === '1'; } catch (e) {}
      const rBtn = document.getElementById('btn-rotate');
      if (rBtn) rBtn.addEventListener('click', () => {
        if (global.Sound) Sound.unlock();
        Interaction.setTableMode(!Interaction.tableMode);
        this.syncRotate();
      });

      // ---- hover ticks on interactive controls ----
      document.querySelectorAll('.menu-btn, .icon-btn, .side-btn').forEach((b) =>
        b.addEventListener('mouseenter', () => { if (global.Sound) Sound.hover(); }));

      this.toStart();
    },

    openAISetup() {
      this.modeButtons.classList.add('hidden');
      this.aiSetup.classList.remove('hidden');
      this.ensureNet();
      if (global.Book) Book.load();   // preload the opening book (best-effort; both evals use it)
    },
    closeAISetup() {
      this.aiSetup.classList.add('hidden');
      this.modeButtons.classList.remove('hidden');
    },

    // fetch the net once, load it into the main instance (eval bar) and the
    // worker (search). Static-hosting friendly (relative models/ path).
    _netLoaded: false,
    async ensureNet() {
      if (this._netLoaded) { this.aiStatus.textContent = 'Model loaded \u2713'; return true; }
      this.aiStatus.textContent = 'Loading model…';
      try {
        const r = await Engine.fetchNet();
        if (!r.ok) { this.aiStatus.textContent = 'No model — ' + (r.error || 'add frontend/models/net.nnue'); return false; }
        const okMain = Engine.loadNNUE(r.bytes);
        let okW = true;
        if (window.AIWorker && AIWorker.available()) okW = await AIWorker.loadNet(r.bytes);
        this._netLoaded = okMain && okW;
        this.aiStatus.textContent = this._netLoaded
          ? `Model loaded \u2713 (${r.name}, ${(r.size/1e6).toFixed(1)} MB)` : 'Failed to load model';
        return this._netLoaded;
      } catch (e) {
        this.aiStatus.textContent = 'Open via a server (not file://) so the model can load';
        return false;
      }
    },

    async startAI() {
      const key = document.getElementById('ai-model').value;
      const preset = MODELS[key] || MODELS.AI1;
      const useNnue = preset.eval === 'nnue';
      // NNUE models REQUIRE the net. If it can't load we refuse to start rather
      // than silently play HCE — the two evals never substitute for each other.
      if (useNnue) {
        const ok = await this.ensureNet();
        if (!ok) {
          this.aiStatus.textContent = 'Can’t start an NNUE model — the net failed to load. Serve over http:// (not file://) and check models/net.nnue.';
          return;
        }
      }
      if (global.Book) await Book.load();     // best-effort; book moves play before search
      console.log(`[setup] ${key} (${preset.label}) · eval ${preset.eval} · depth ${preset.depth} · ${preset.timeMs} ms · book ${global.Book && Book.ready ? 'on' : 'off'}`);
      this.startGame('pvai', this.aiSide, preset.depth, preset.timeMs, preset.label, useNnue);
    },

    startGame(mode, side, depth, timeMs, modelName, useNnue) {
      if (global.Sound) Sound.unlock();   // enable audio (we're inside a click)
      Engine.newGame();
      Interaction.reset(mode || 'pvp', side || 0, depth || 5, timeMs || 0, modelName || '', !!useNnue);
      this.started = true;
      this.banner.classList.add('hidden');
      this.startScreen.classList.add('hidden');
      this.gameScreen.classList.remove('hidden');
      const rBtn = document.getElementById('btn-rotate');   // rotation toggle: HvH only
      if (rBtn) rBtn.classList.toggle('hidden', (mode || 'pvp') !== 'pvp');
      this.syncRotate();
    },

    syncRotate() {
      const rBtn = document.getElementById('btn-rotate');
      if (!rBtn) return;
      const on = Interaction.tableMode;
      rBtn.innerHTML = on ? IC.rotateOff : IC.rotateOn;
      rBtn.classList.toggle('active', on);
      rBtn.title = on ? 'Table mode on — board fixed, pieces rotate' : 'Board flips each turn (tap for table mode)';
    },

    toStart() {
      this.started = false;
      if (global.Interaction) Interaction.halt();   // fully stop play before returning home
      if (this.aiStatus) this.aiStatus.textContent = '';
      this.banner.classList.add('hidden');
      this.gameScreen.classList.add('hidden');
      this.aiSetup.classList.add('hidden');
      this.modeButtons.classList.remove('hidden');
      this.startScreen.classList.remove('hidden');
    }
  };

  global.Controls = Controls;
})(window);
