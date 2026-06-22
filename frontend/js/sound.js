/* sound.js — game audio via the Web Audio API.
 *
 * Why Web Audio (not <audio> elements): decoded buffers played through an
 * AudioContext do NOT register as an OS "media player", so there's no phone
 * status-bar playback notification, and backgrounding can be handled cleanly by
 * suspending the context. Files are prefetched on load and decoded on the first
 * user gesture (browsers block audio until then). Preferences persist. */
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
    gameend: 'game-end.webm',
    hover:   'button-hover.mp3',
  };
  const BGM = 'bgm.mp3';

  function getPref(k, d) { try { const v = localStorage.getItem(k); return v === null ? d : v === '1'; } catch (e) { return d; } }
  function setPref(k, on) { try { localStorage.setItem(k, on ? '1' : '0'); } catch (e) {} }

  let ctx = null, sfxGain = null, musicGain = null;
  let bgmBuf = null, bgmSrc = null;
  const buffers = {};          // kind -> decoded AudioBuffer
  const raw = {};              // kind / "__bgm" -> prefetched ArrayBuffer
  let unlocked = false, decoded = false;

  // Prefetch the raw bytes immediately (no AudioContext needed for this).
  const rawReady = (async () => {
    const grab = (name) => fetch(BASE + name).then((r) => r.arrayBuffer()).catch(() => null);
    const jobs = Object.keys(FILES).map((k) => grab(FILES[k]).then((b) => { raw[k] = b; }));
    jobs.push(grab(BGM).then((b) => { raw.__bgm = b; }));
    await Promise.all(jobs);
  })();

  function ensureCtx() {
    if (ctx) return ctx;
    const AC = global.AudioContext || global.webkitAudioContext;
    if (!AC) return null;
    ctx = new AC();
    sfxGain = ctx.createGain();   sfxGain.gain.value = 0.9;    sfxGain.connect(ctx.destination);
    musicGain = ctx.createGain(); musicGain.gain.value = 0.30; musicGain.connect(ctx.destination);
    return ctx;
  }

  const decode = (ab) => new Promise((res) => {
    if (!ab) { res(null); return; }
    try { ctx.decodeAudioData(ab, res, () => res(null)); } catch (e) { res(null); }
  });

  async function decodeAll() {
    if (decoded || !ctx) return;
    await rawReady;
    for (const k of Object.keys(FILES)) buffers[k] = await decode(raw[k]);
    bgmBuf = await decode(raw.__bgm);
    decoded = true;
  }

  function startBgm() {
    if (!ctx || !bgmBuf || bgmSrc) return;
    bgmSrc = ctx.createBufferSource();
    bgmSrc.buffer = bgmBuf; bgmSrc.loop = true;
    bgmSrc.connect(musicGain); bgmSrc.start();
  }
  function stopBgm() { if (bgmSrc) { try { bgmSrc.stop(); } catch (e) {} bgmSrc.disconnect(); bgmSrc = null; } }

  const Sound = {
    effects: getPref('snd.fx', true),
    music:   getPref('snd.music', true),

    // Call from a user gesture. Creates/resumes the context, decodes, starts music.
    unlock() {
      unlocked = true;
      if (!ensureCtx()) return;
      if (ctx.state === 'suspended') ctx.resume();
      decodeAll().then(() => { if (this.music && !document.hidden) startBgm(); });
    },

    play(kind) {
      if (!this.effects || !ctx || !decoded) return;
      const buf = buffers[FILES[kind] ? kind : 'move'];
      if (!buf) return;
      const s = ctx.createBufferSource();
      s.buffer = buf; s.connect(sfxGain); s.start();
    },
    hover() { this.play('hover'); },

    setMusic(on) {
      this.music = on; setPref('snd.music', on);
      if (on) { if (unlocked && !document.hidden) startBgm(); } else { stopBgm(); }
      return on;
    },
    setEffects(on) { this.effects = on; setPref('snd.fx', on); return on; },
    toggleMusic()   { return this.setMusic(!this.music); },
    toggleEffects() { return this.setEffects(!this.effects); },
  };

  // Stop audio entirely when backgrounded (phone minimised / tab hidden); resume
  // when it returns. Suspending the context also frees the audio hardware.
  document.addEventListener('visibilitychange', () => {
    if (document.hidden) {
      stopBgm();
      if (ctx && ctx.suspend) ctx.suspend();
    } else if (ctx) {
      if (ctx.resume) ctx.resume();
      if (Sound.music && unlocked) startBgm();
    }
  });

  global.Sound = Sound;
})(window);
