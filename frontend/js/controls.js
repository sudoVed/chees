/* controls.js — screen flow + top-bar actions.
 * Human vs Human, or Play with AI (pick model + side; model = depth preset). */
(function (global) {
  'use strict';

  // ===========================================================================
  // AI MODELS — the four selectable opponents. EDIT EVERYTHING HERE (one place):
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
  const MODELS = {
    AI1: { label: 'Pragg-nunnu',  eval: 'nnue', depth: 3,  timeMs: 1500, desc: 'Neural-net eval · minimum search' },
    AI2: { label: 'Gary Gas-ka-price',  eval: 'nnue', depth: 5,  timeMs: 1500, desc: 'Neural-net eval · shallow search' },
    AI3: { label: 'Bobby Seizure', eval: 'nnue', depth: 10, timeMs: 4000, desc: 'Neural-net eval · deeper search' },
    AI4: { label: 'Gukesh Gutka-wala',   eval: 'hce',  depth: 3,  timeMs: 1500, desc: 'Hand-crafted eval · minimum search' },
    AI5: { label: 'Hikaru Hakla',   eval: 'hce',  depth: 5,  timeMs: 1500, desc: 'Hand-crafted eval · shallow search' },
    AI6: { label: 'Magnus Murgi-chor',  eval: 'hce',  depth: 10, timeMs: 4000, desc: 'Hand-crafted eval · deeper search' },
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
        if (!this.started || Interaction.thinking) return;
        if (!Engine.undo()) return;
        if (Interaction.mode === 'pvai' && Engine.sideToMove() !== Interaction.humanColor) Engine.undo();
        Interaction.selected = -1; Interaction.legal = []; Interaction.gameOver = false;
        Interaction.refresh();
      });
      document.getElementById('btn-exit').addEventListener('click', () => this.toStart());
      document.getElementById('btn-result-new').addEventListener('click', () => this.toStart());

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
      Engine.newGame();
      const log = document.getElementById('ai-log'); if (log) log.textContent = '';   // don't carry a stale line into a new game
      Interaction.reset(mode || 'pvp', side || 0, depth || 5, timeMs || 0, modelName || '', !!useNnue);
      this.started = true;
      this.banner.classList.add('hidden');
      this.startScreen.classList.add('hidden');
      this.gameScreen.classList.remove('hidden');
    },

    toStart() {
      this.started = false;
      const log = document.getElementById('ai-log'); if (log) log.textContent = '';   // clear the move line on exit
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
