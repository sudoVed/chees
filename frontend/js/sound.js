/* sound.js — real audio: move/capture/etc. sound effects plus looping background
 * music, with independent toggles. Files live in assets/. Browsers block audio
 * until a user gesture, so unlock() is called from the first click. Preferences
 * are remembered in localStorage. */
(function (global) {
  'use strict';

  const BASE = 'assets/';
  const FILES = {
    move:    'move-self.mp3',
    capture: 'capture.mp3',
    castle:  'castle.mp3',
    promote: 'promote.mp3',
    check:   'move-check.mp3',
    notify:  'notify.mp3',
    hover:   'button-hover.mp3',
  };

  function getPref(key, def) {
    try { const v = localStorage.getItem(key); return v === null ? def : v === '1'; }
    catch (e) { return def; }
  }
  function setPref(key, on) { try { localStorage.setItem(key, on ? '1' : '0'); } catch (e) {} }

  const sfx = {};                 // lazily-created <audio> per effect
  function el(kind) {
    if (!sfx[kind]) { const a = new Audio(BASE + FILES[kind]); a.preload = 'auto'; sfx[kind] = a; }
    return sfx[kind];
  }

  const bgm = new Audio(BASE + 'bgm.mp3');
  bgm.loop = true; bgm.volume = 0.30; bgm.preload = 'auto';

  let unlocked = false;

  const Sound = {
    effects: getPref('snd.fx', true),
    music:   getPref('snd.music', true),

    // call once from a user gesture; warms the audio and starts music if enabled
    unlock() {
      if (unlocked) return;
      unlocked = true;
      for (const k in FILES) el(k);
      if (this.music) this._playMusic();
    },

    play(kind) {
      if (!this.effects || !unlocked) return;
      const a = el(FILES[kind] ? kind : 'move');
      try { a.currentTime = 0; const p = a.play(); if (p && p.catch) p.catch(() => {}); } catch (e) {}
    },
    hover() { this.play('hover'); },

    _playMusic() { try { const p = bgm.play(); if (p && p.catch) p.catch(() => {}); } catch (e) {} },

    setMusic(on) {
      this.music = on; setPref('snd.music', on);
      if (on) { if (unlocked) this._playMusic(); } else { bgm.pause(); }
      return on;
    },
    setEffects(on) { this.effects = on; setPref('snd.fx', on); return on; },
    toggleMusic()   { return this.setMusic(!this.music); },
    toggleEffects() { return this.setEffects(!this.effects); },
  };

  global.Sound = Sound;
})(window);
