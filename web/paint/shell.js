// The page shell's loader (src/host/web/README.md): cross-origin isolation,
// the program, its asset packs and its font groups, and the persistent home
// directory. Everything it loads is listed in page.json beside it.
//
// page.json:
// {
//   "program": "plan_paint.js",            the Emscripten output (its .wasm beside it)
//   "version": "2026.11.15-1",             appended to every URL (?v=), for caching
//   "arguments": [],                        passed to main
//   "persist": "/home/web_user",            an IndexedDB-backed directory, or omitted
//   "packs": [                              file_packager outputs (--js-output)
//     { "script": "assets.js", "required": true }
//   ],
//   "fonts": {
//     "required": [ { "script": "fonts-base.js", "group": "base" } ],
//     "lazy": [ { "script": "fonts-cjk.js", "group": "cjk", "sentinel": "/gui_forms/fonts/cjk.ok" } ]
//   }
// }
//
// Required packs and font groups load before main runs (the program's
// bundled interface fonts must be present when its first window opens);
// lazy font groups load afterwards, and the program hears of each
// (gf_web_post_fonts) once its sentinel file exists.
'use strict';

(function () {
  const statusBox = document.getElementById('gui-forms-status');
  const statusText = document.getElementById('gui-forms-status-text');
  const progress = document.getElementById('gui-forms-progress');

  function setStatus(text) {
    statusText.textContent = text;
  }

  function fail(text) {
    statusBox.hidden = false;
    progress.hidden = true;
    statusText.className = 'gui-forms-error';
    statusText.textContent = text;
  }

  // SharedArrayBuffer needs a cross-origin isolated page. The server sends
  // COOP and COEP (.htaccess); where a proxy drops them, the service worker
  // adds them and the page reloads once under it.
  async function ensureIsolation() {
    if (self.crossOriginIsolated) return true;
    if (!('serviceWorker' in navigator)) return false;
    const reloaded = sessionStorage.getItem('gui-forms-coi-reload') === '1';
    if (reloaded) {
      sessionStorage.removeItem('gui-forms-coi-reload');
      return false;
    }
    try {
      await navigator.serviceWorker.register('coi-serviceworker.js', { scope: './' });
      await navigator.serviceWorker.ready;
    } catch (error) {
      console.error('GUI.Forms: the isolation service worker did not register', error);
      return false;
    }
    sessionStorage.setItem('gui-forms-coi-reload', '1');
    location.reload();
    return new Promise(() => {});
  }

  function loadScript(url) {
    return new Promise((resolve, reject) => {
      const script = document.createElement('script');
      script.src = url;
      script.async = false;
      script.onload = () => resolve();
      script.onerror = () => reject(new Error('cannot load ' + url));
      document.head.append(script);
    });
  }

  function versioned(url, version) {
    return version ? url + (url.includes('?') ? '&' : '?') + 'v=' + encodeURIComponent(version) : url;
  }

  // Waits for a lazy font group's sentinel file, then tells the program.
  function watchFontGroup(group) {
    let attempts = 0;
    const check = () => {
      attempts += 1;
      let present = false;
      try {
        present = Module.FS.analyzePath(group.sentinel).exists;
      } catch (error) {
        present = false;
      }
      if (present) {
        const name = Module.stringToNewUTF8(group.group);
        try {
          Module._gf_web_post_fonts(name);
        } finally {
          Module._free(name);
        }
        return;
      }
      if (attempts < 600) setTimeout(check, 100);
      else console.warn('GUI.Forms: the font group ' + group.group + ' did not arrive');
    };
    check();
  }

  function persist(path) {
    if (!path) return;
    let busy = false;
    const flush = () => {
      if (busy) return;
      busy = true;
      Module.FS.syncfs(false, (error) => {
        busy = false;
        if (error) console.warn('GUI.Forms: saving the home directory failed', error);
      });
    };
    setInterval(flush, 30000);
    document.addEventListener('visibilitychange', () => { if (document.hidden) flush(); });
    window.addEventListener('pagehide', flush);
  }

  async function start() {
    if (!(await ensureIsolation())) {
      fail('This browser cannot run Plan Paint here: the page is not cross-origin isolated, which ' +
           'shared memory and threads need. Try a current Chrome, Edge, Firefox or Safari.');
      return;
    }
    if (!self.WebAssembly || !document.createElement('canvas').getContext('webgl2')) {
      fail('Plan Paint needs WebAssembly and WebGL2, which this browser does not offer.');
      return;
    }
    let page;
    try {
      const response = await fetch('page.json', { cache: 'no-cache' });
      page = await response.json();
    } catch (error) {
      fail('Plan Paint could not be loaded (page.json): ' + error.message);
      return;
    }
    const version = page.version || '';
    const persistPath = page.persist || '';
    const fonts = page.fonts || {};
    let totalDependencies = 0;
    window.Module = {
      guiFormsCanvas: document.getElementById('gui-forms-canvas'),
      noInitialRun: true,
      locateFile: (path) => versioned(path, version),
      print: (text) => console.log(text),
      printErr: (text) => console.error(text),
      setStatus: (text) => { if (text) setStatus(text); },
      monitorRunDependencies: (left) => {
        totalDependencies = Math.max(totalDependencies, left);
        progress.value = totalDependencies === 0 ? 1 : (totalDependencies - left) / totalDependencies;
      },
      preRun: [() => {
        if (!persistPath) return;
        Module.FS.mkdirTree(persistPath);
        Module.FS.mount(Module.IDBFS, {}, persistPath);
        Module.addRunDependency('gui-forms-persist');
        Module.FS.syncfs(true, (error) => {
          if (error) console.warn('GUI.Forms: reading the home directory failed', error);
          Module.removeRunDependency('gui-forms-persist');
        });
      }],
      onRuntimeInitialized: () => {
        statusBox.hidden = true;
        persist(persistPath);
        Module.callMain(page.arguments || []);
        const lazy = fonts.lazy || [];
        for (const group of lazy) {
          loadScript(versioned(group.script, version)).then(() => watchFontGroup(group),
            (error) => console.warn('GUI.Forms: ' + error.message));
        }
      },
      onAbort: (reason) => fail('Plan Paint stopped: ' + reason),
    };
    try {
      setStatus('Loading…');
      // Pack loaders register their files with Module before the program
      // script runs (file_packager --js-output).
      for (const pack of page.packs || []) {
        if (pack.required !== false) await loadScript(versioned(pack.script, version));
      }
      for (const group of fonts.required || []) await loadScript(versioned(group.script, version));
      await loadScript(versioned(page.program, version));
      for (const pack of page.packs || []) {
        if (pack.required === false) loadScript(versioned(pack.script, version));
      }
    } catch (error) {
      fail('Plan Paint could not be loaded: ' + error.message);
    }
  }

  start();
})();
