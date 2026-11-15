// The browser host's page half (Emscripten --js-library; the program half
// is src/host/web/application). Everything here runs on the browser's main
// thread: the program's calls (gf_web_js_*) are proxied to it, "sync" ones
// blocking the program's thread until they return. The page posts what
// happens back through the gf_web_post_* exports (web_page_exports.cpp)
// and presents the frames the program hands over in the present record
// (web_present.hpp, whose offsets are repeated below).
//
// Presentation is WebGL2 only (owner, 2026-10-27 and 2026-11-15): each
// surface is a texture, its damaged rectangles uploaded from the program's
// raster (premultiplied BGRA bytes, uploaded as RGBA and swizzled in the
// shader), the layers drawn back to front with premultiplied blending.

addToLibrary({
  $GFWeb__deps: ['$FS', '$stringToNewUTF8', '$UTF8ToString'],
  $GFWeb: {
    canvas: null,
    gl: null,
    program: null,
    uniforms: null,
    textures: new Map(),
    // The present record's address in the module's memory.
    frame: 0,
    presentScheduled: false,
    framesWanted: false,
    // A frame handed over while the document was hidden is presented from
    // a timer: animation frames do not run then, and the program waits.
    input: null,
    composing: false,
    skipInput: false,
    pendingPaste: null,
    clipboard: '',
    cursor: 'default',
    cursorHidden: false,
    saves: new Map(),
    scratch: null,
    copyUploads: false,
    // web_present.hpp offsets (bytes).
    SURFACES: 32,
    SURFACE_BYTES: 32,
    LAYERS: 32 + 32 * 160,
    LAYER_BYTES: 32,
    RECTS: 32 + 32 * 160 + 32 * 320,
    RELEASED: 32 + 32 * 160 + 32 * 320 + 16 * 1024,

    // gui_forms::Modifier bits: shift 1, control 2, alt 4, meta 8.
    modifiers(event) {
      return (event.shiftKey ? 1 : 0) | (event.ctrlKey ? 2 : 0) | (event.altKey ? 4 : 0) | (event.metaKey ? 8 : 0);
    },

    postString(target, text) {
      const pointer = stringToNewUTF8(text);
      try {
        target(pointer);
      } finally {
        _free(pointer);
      }
    },

    position(event) {
      const box = GFWeb.canvas.getBoundingClientRect();
      return [event.clientX - box.left, event.clientY - box.top];
    },

    // A file name the program's file system takes: no separators, no line
    // breaks (paths are posted one per line).
    safeName(name) {
      let cleaned = String(name).replace(/[\/\\\n\r\0]/g, '_');
      if (cleaned === '' || cleaned === '.' || cleaned === '..') cleaned = 'file';
      return cleaned;
    },

    postDialog(request, outcome, paths, text) {
      const joined = stringToNewUTF8(paths.join('\n'));
      const value = stringToNewUTF8(text || '');
      try {
        _gf_web_post_dialog(request, outcome, joined, value);
      } finally {
        _free(joined);
        _free(value);
      }
    },

    // Writes `files` (File objects) under `directory` and returns their
    // paths.
    async writeFiles(files, directory) {
      FS.mkdirTree(directory);
      const paths = [];
      for (let index = 0; index < files.length; ++index) {
        const file = files[index];
        const bytes = new Uint8Array(await file.arrayBuffer());
        let path = directory + '/' + GFWeb.safeName(file.name);
        while (FS.analyzePath(path).exists) path = directory + '/' + index + '-' + GFWeb.safeName(file.name);
        FS.writeFile(path, bytes);
        paths.push(path);
      }
      return paths;
    },

    // A browser chooser needs a recent user gesture on the page. When the
    // gesture that asked for it has lapsed, a small prompt asks for one.
    withActivation(label, start, cancel) {
      const activation = navigator.userActivation;
      if (!activation || activation.isActive) {
        start();
        return;
      }
      const prompt = document.createElement('div');
      prompt.className = 'gui-forms-prompt';
      const go = document.createElement('button');
      go.textContent = label;
      const stop = document.createElement('button');
      stop.textContent = 'Cancel';
      prompt.append(go, stop);
      go.addEventListener('click', () => { prompt.remove(); start(); });
      stop.addEventListener('click', () => { prompt.remove(); cancel(); });
      (GFWeb.canvas.parentElement || document.body).append(prompt);
      go.focus();
    },

    // A saved file reached the page: the chosen file handle where the
    // browser has a save chooser, else a download.
    async finishSave(path) {
      const save = GFWeb.saves.get(path);
      if (!save) return;
      GFWeb.saves.delete(path);
      let bytes;
      try {
        bytes = FS.readFile(path);
      } catch (error) {
        console.error('GUI.Forms: the saved file ' + path + ' cannot be read', error);
        return;
      }
      try {
        if (save.handle) {
          const writable = await save.handle.createWritable();
          await writable.write(bytes);
          await writable.close();
        } else {
          const url = URL.createObjectURL(new Blob([bytes]));
          const link = document.createElement('a');
          link.href = url;
          link.download = save.name;
          document.body.append(link);
          link.click();
          link.remove();
          setTimeout(() => URL.revokeObjectURL(url), 60000);
        }
      } catch (error) {
        console.error('GUI.Forms: saving ' + save.name + ' failed', error);
      }
      try {
        FS.unlink(path);
      } catch (error) {
      }
    },

    // FS.close's hook for saves: the program closing a file it wrote at a
    // save dialog's path delivers it.
    hookClose() {
      const close = FS.close;
      FS.close = function (stream) {
        const path = stream.path;
        const written = (stream.flags & 3) !== 0;
        const result = close.call(FS, stream);
        if (written && GFWeb.saves.has(path)) GFWeb.finishSave(path);
        return result;
      };
    },

    compile(type, source) {
      const gl = GFWeb.gl;
      const shader = gl.createShader(type);
      gl.shaderSource(shader, source);
      gl.compileShader(shader);
      if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(shader));
      return shader;
    },

    setUpGl() {
      const gl = GFWeb.canvas.getContext('webgl2', {
        alpha: false, premultipliedAlpha: true, antialias: false, depth: false, stencil: false,
        preserveDrawingBuffer: false, desynchronized: false,
      });
      if (!gl) throw new Error('GUI.Forms needs WebGL2');
      GFWeb.gl = gl;
      const vertex = GFWeb.compile(gl.VERTEX_SHADER, `#version 300 es
        in vec2 a_unit;
        uniform vec4 u_rect;
        uniform vec2 u_canvas;
        out vec2 v_uv;
        void main() {
          vec2 device = u_rect.xy + a_unit * u_rect.zw;
          vec2 clip = device / u_canvas * 2.0 - 1.0;
          gl_Position = vec4(clip.x, -clip.y, 0.0, 1.0);
          v_uv = a_unit;
        }`);
      // The raster's bytes are B, G, R, A: uploaded as RGBA they read back
      // swizzled.
      const fragment = GFWeb.compile(gl.FRAGMENT_SHADER, `#version 300 es
        precision mediump float;
        uniform sampler2D u_texture;
        uniform int u_mode;
        uniform int u_opaque;
        uniform vec4 u_color;
        in vec2 v_uv;
        out vec4 colour;
        void main() {
          if (u_mode == 1) { colour = u_color; return; }
          vec4 texel = texture(u_texture, v_uv).bgra;
          if (u_opaque == 1) texel.a = 1.0;
          colour = texel;
        }`);
      const program = gl.createProgram();
      gl.attachShader(program, vertex);
      gl.attachShader(program, fragment);
      gl.bindAttribLocation(program, 0, 'a_unit');
      gl.linkProgram(program);
      if (!gl.getProgramParameter(program, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(program));
      GFWeb.program = program;
      GFWeb.uniforms = {
        rect: gl.getUniformLocation(program, 'u_rect'),
        canvas: gl.getUniformLocation(program, 'u_canvas'),
        mode: gl.getUniformLocation(program, 'u_mode'),
        opaque: gl.getUniformLocation(program, 'u_opaque'),
        color: gl.getUniformLocation(program, 'u_color'),
        texture: gl.getUniformLocation(program, 'u_texture'),
      };
      const buffer = gl.createBuffer();
      gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
      gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([0, 0, 1, 0, 0, 1, 1, 1]), gl.STATIC_DRAW);
      gl.enableVertexAttribArray(0);
      gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);
      gl.useProgram(program);
      gl.uniform1i(GFWeb.uniforms.texture, 0);
      gl.enable(gl.BLEND);
      gl.blendFunc(gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
      gl.pixelStorei(gl.UNPACK_ALIGNMENT, 4);
      // WebGL refuses views of a SharedArrayBuffer in some browsers: those
      // uploads go through an ordinary scratch copy.
      GFWeb.copyUploads = typeof SharedArrayBuffer !== 'undefined' && HEAPU8.buffer instanceof SharedArrayBuffer;
      GFWeb.canvas.addEventListener('webglcontextlost', (event) => event.preventDefault());
      GFWeb.canvas.addEventListener('webglcontextrestored', () => {
        GFWeb.textures.clear();
        GFWeb.setUpGl();
      });
    },

    upload(texture, pixels, rowBytes, left, top, right, bottom) {
      const gl = GFWeb.gl;
      const width = right - left;
      const height = bottom - top;
      if (width <= 0 || height <= 0) return;
      gl.bindTexture(gl.TEXTURE_2D, texture);
      const first = pixels + top * rowBytes + left * 4;
      if (!GFWeb.copyUploads) {
        try {
          gl.pixelStorei(gl.UNPACK_ROW_LENGTH, rowBytes >> 2);
          gl.texSubImage2D(gl.TEXTURE_2D, 0, left, top, width, height, gl.RGBA, gl.UNSIGNED_BYTE, HEAPU8, first);
          gl.pixelStorei(gl.UNPACK_ROW_LENGTH, 0);
          return;
        } catch (error) {
          gl.pixelStorei(gl.UNPACK_ROW_LENGTH, 0);
          GFWeb.copyUploads = true;
        }
      }
      const bytes = width * height * 4;
      if (!GFWeb.scratch || GFWeb.scratch.length < bytes) GFWeb.scratch = new Uint8Array(Math.max(bytes, 1 << 20));
      const scratch = GFWeb.scratch;
      const rowLength = width * 4;
      for (let row = 0; row < height; ++row) {
        const from = first + row * rowBytes;
        scratch.set(HEAPU8.subarray(from, from + rowLength), row * rowLength);
      }
      gl.texSubImage2D(gl.TEXTURE_2D, 0, left, top, width, height, gl.RGBA, gl.UNSIGNED_BYTE, scratch, 0);
    },

    present() {
      GFWeb.presentScheduled = false;
      const base = GFWeb.frame;
      if (Atomics.load(HEAPU32, base >> 2) !== 1) return;
      const gl = GFWeb.gl;
      const word = base >> 2;
      const canvasWidth = HEAPU32[word + 1];
      const canvasHeight = HEAPU32[word + 2];
      const surfaceCount = HEAPU32[word + 3];
      const layerCount = HEAPU32[word + 4];
      const releasedCount = HEAPU32[word + 6];
      const background = HEAPU32[word + 7];
      if (GFWeb.canvas.width !== canvasWidth) GFWeb.canvas.width = canvasWidth;
      if (GFWeb.canvas.height !== canvasHeight) GFWeb.canvas.height = canvasHeight;
      for (let index = 0; index < surfaceCount; ++index) {
        const at = (base + GFWeb.SURFACES + index * GFWeb.SURFACE_BYTES) >> 2;
        const id = HEAPU32[at];
        const pixels = HEAPU32[at + 1];
        const rowBytes = HEAPU32[at + 2];
        const width = HEAPU32[at + 3];
        const height = HEAPU32[at + 4];
        const firstRect = HEAPU32[at + 5];
        const rectCount = HEAPU32[at + 6];
        const alpha = HEAPU32[at + 7];
        let entry = GFWeb.textures.get(id);
        let whole = false;
        if (!entry || entry.width !== width || entry.height !== height) {
          if (entry) gl.deleteTexture(entry.texture);
          const texture = gl.createTexture();
          gl.bindTexture(gl.TEXTURE_2D, texture);
          gl.texStorage2D(gl.TEXTURE_2D, 1, gl.RGBA8, width, height);
          gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
          gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
          gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
          gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
          entry = { texture: texture, width: width, height: height, alpha: alpha };
          GFWeb.textures.set(id, entry);
          whole = true;
        }
        entry.alpha = alpha;
        if (whole) {
          GFWeb.upload(entry.texture, pixels, rowBytes, 0, 0, width, height);
          continue;
        }
        for (let rect = 0; rect < rectCount; ++rect) {
          const r = (base + GFWeb.RECTS) / 4 + (firstRect + rect) * 4;
          GFWeb.upload(entry.texture, pixels, rowBytes, HEAP32[r], HEAP32[r + 1], HEAP32[r + 2], HEAP32[r + 3]);
        }
      }
      for (let index = 0; index < releasedCount; ++index) {
        const id = HEAPU32[(base + GFWeb.RELEASED) / 4 + index];
        const entry = GFWeb.textures.get(id);
        if (entry) gl.deleteTexture(entry.texture);
        GFWeb.textures.delete(id);
      }
      gl.viewport(0, 0, canvasWidth, canvasHeight);
      gl.clearColor(((background >>> 24) & 255) / 255, ((background >>> 16) & 255) / 255,
                    ((background >>> 8) & 255) / 255, 1.0);
      gl.clear(gl.COLOR_BUFFER_BIT);
      gl.useProgram(GFWeb.program);
      gl.uniform2f(GFWeb.uniforms.canvas, canvasWidth, canvasHeight);
      gl.activeTexture(gl.TEXTURE0);
      for (let index = 0; index < layerCount; ++index) {
        const at = (base + GFWeb.LAYERS + index * GFWeb.LAYER_BYTES) >> 2;
        const kind = HEAPU32[at];
        gl.uniform4f(GFWeb.uniforms.rect, HEAPF32[at + 4], HEAPF32[at + 5], HEAPF32[at + 6], HEAPF32[at + 7]);
        if (kind === 1) {
          const colour = HEAPU32[at + 2];
          const a = (colour & 255) / 255;
          gl.uniform1i(GFWeb.uniforms.mode, 1);
          gl.uniform4f(GFWeb.uniforms.color, ((colour >>> 24) & 255) / 255 * a, ((colour >>> 16) & 255) / 255 * a,
                       ((colour >>> 8) & 255) / 255 * a, a);
        } else {
          const entry = GFWeb.textures.get(HEAPU32[at + 1]);
          if (!entry) continue;
          gl.uniform1i(GFWeb.uniforms.mode, 0);
          gl.uniform1i(GFWeb.uniforms.opaque, entry.alpha ? 0 : 1);
          gl.bindTexture(gl.TEXTURE_2D, entry.texture);
        }
        gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);
      }
      Atomics.store(HEAPU32, base >> 2, 0);
      _gf_web_post_presented();
    },

    tick(time) {
      GFWeb.presentScheduled = false;
      GFWeb.present();
      if (GFWeb.framesWanted) {
        _gf_web_post_frame(time);
        GFWeb.schedule();
      }
    },

    schedule() {
      if (GFWeb.presentScheduled) return;
      GFWeb.presentScheduled = true;
      if (document.hidden) {
        setTimeout(() => GFWeb.tick(performance.now()), 0);
      } else {
        requestAnimationFrame(GFWeb.tick);
      }
    },

    postResize() {
      const box = GFWeb.canvas.getBoundingClientRect();
      _gf_web_post_resize(Math.max(1, box.width), Math.max(1, box.height), window.devicePixelRatio || 1);
    },

    watchScale() {
      const query = window.matchMedia('(resolution: ' + (window.devicePixelRatio || 1) + 'dppx)');
      query.addEventListener('change', () => { GFWeb.postResize(); GFWeb.watchScale(); }, { once: true });
    },

    pointer(action, event) {
      const position = GFWeb.position(event);
      _gf_web_post_pointer(action, position[0], position[1], event.button, event.pointerId,
                           GFWeb.modifiers(event), event.timeStamp);
    },

    // Keys the browser keeps: printable text goes on to the input element
    // (its input event carries the text), and copy, cut and paste reach
    // their clipboard events. Every other key is the program's.
    browserKeeps(event) {
      const command = event.ctrlKey || event.metaKey;
      if (command && !event.altKey && (event.code === 'KeyC' || event.code === 'KeyX' || event.code === 'KeyV'))
        return true;
      return !command && !event.altKey && event.key.length === 1;
    },

    postKey(down, event) {
      const code = stringToNewUTF8(event.code || '');
      try {
        _gf_web_post_key(down ? 1 : 0, event.repeat ? 1 : 0, code, GFWeb.modifiers(event));
      } finally {
        _free(code);
      }
    },

    flushPaste() {
      const pending = GFWeb.pendingPaste;
      if (!pending) return;
      GFWeb.pendingPaste = null;
      clearTimeout(pending.timer);
      GFWeb.postKey(true, pending.event);
    },

    listen() {
      const canvas = GFWeb.canvas;
      const input = document.createElement('textarea');
      input.className = 'gui-forms-input';
      input.setAttribute('autocomplete', 'off');
      input.setAttribute('autocapitalize', 'off');
      input.setAttribute('spellcheck', 'false');
      input.setAttribute('aria-hidden', 'true');
      input.style.cssText = 'position:absolute;left:0;top:0;width:1px;height:1px;opacity:0;padding:0;border:0;' +
                            'resize:none;overflow:hidden;white-space:pre;z-index:-1;';
      (canvas.parentElement || document.body).append(input);
      GFWeb.input = input;
      canvas.tabIndex = -1;
      canvas.style.touchAction = 'none';
      canvas.addEventListener('pointerdown', (event) => {
        input.focus({ preventScroll: true });
        canvas.setPointerCapture(event.pointerId);
        GFWeb.pointer(1, event);
        event.preventDefault();
      });
      canvas.addEventListener('pointermove', (event) => GFWeb.pointer(0, event));
      canvas.addEventListener('pointerup', (event) => GFWeb.pointer(2, event));
      canvas.addEventListener('pointercancel', (event) => GFWeb.pointer(5, event));
      canvas.addEventListener('pointerenter', (event) => GFWeb.pointer(3, event));
      canvas.addEventListener('pointerleave', (event) => GFWeb.pointer(4, event));
      canvas.addEventListener('contextmenu', (event) => event.preventDefault());
      canvas.addEventListener('wheel', (event) => {
        const position = GFWeb.position(event);
        _gf_web_post_wheel(position[0], position[1], event.deltaX, event.deltaY, event.deltaMode,
                           GFWeb.modifiers(event));
        event.preventDefault();
      }, { passive: false });
      input.addEventListener('keydown', (event) => {
        if (event.isComposing || event.keyCode === 229) return;
        GFWeb.skipInput = false;
        const command = event.ctrlKey || event.metaKey;
        if (command && event.code === 'KeyV' && !event.altKey) {
          // The key waits for the paste event, so the program reads the
          // pasted text when it handles the key.
          GFWeb.flushPaste();
          GFWeb.pendingPaste = { event: event, timer: setTimeout(GFWeb.flushPaste, 50) };
          return;
        }
        GFWeb.postKey(true, event);
        if (!GFWeb.browserKeeps(event)) event.preventDefault();
      });
      input.addEventListener('keyup', (event) => {
        if (event.isComposing || event.keyCode === 229) return;
        GFWeb.flushPaste();
        GFWeb.postKey(false, event);
        if (!GFWeb.browserKeeps(event)) event.preventDefault();
      });
      input.addEventListener('input', (event) => {
        if (event.isComposing || GFWeb.composing) return;
        const text = input.value;
        input.value = '';
        if (GFWeb.skipInput) {
          GFWeb.skipInput = false;
          return;
        }
        if (text !== '') GFWeb.postString((pointer) => _gf_web_post_text(pointer, 0), text);
      });
      input.addEventListener('compositionstart', () => { GFWeb.composing = true; });
      input.addEventListener('compositionupdate', (event) => {
        GFWeb.postString((pointer) => _gf_web_post_text(pointer, 1), event.data || '');
      });
      input.addEventListener('compositionend', (event) => {
        GFWeb.composing = false;
        GFWeb.skipInput = true;
        input.value = '';
        const text = event.data || '';
        if (text === '') {
          GFWeb.postString((pointer) => _gf_web_post_text(pointer, 1), '');
        } else {
          GFWeb.postString((pointer) => _gf_web_post_text(pointer, 0), text);
        }
      });
      // The program writes the clipboard itself (navigator.clipboard), once
      // it has handled the key.
      input.addEventListener('copy', (event) => event.preventDefault());
      input.addEventListener('cut', (event) => event.preventDefault());
      input.addEventListener('paste', (event) => {
        const text = event.clipboardData ? event.clipboardData.getData('text/plain') : '';
        GFWeb.clipboard = text;
        GFWeb.postString(_gf_web_post_clipboard, text);
        GFWeb.flushPaste();
        event.preventDefault();
      });
      input.addEventListener('focus', () => _gf_web_post_focus(1));
      input.addEventListener('blur', () => _gf_web_post_focus(0));
      canvas.addEventListener('dragover', (event) => {
        event.preventDefault();
        if (event.dataTransfer) event.dataTransfer.dropEffect = 'copy';
      });
      canvas.addEventListener('drop', async (event) => {
        event.preventDefault();
        if (!event.dataTransfer || event.dataTransfer.files.length === 0) return;
        const position = GFWeb.position(event);
        const modifiers = GFWeb.modifiers(event);
        GFWeb.drops = (GFWeb.drops || 0) + 1;
        const paths = await GFWeb.writeFiles(event.dataTransfer.files, '/web/drops/' + GFWeb.drops);
        const joined = stringToNewUTF8(paths.join('\n'));
        try {
          _gf_web_post_drop(position[0], position[1], joined, modifiers);
        } finally {
          _free(joined);
        }
      });
      if (typeof ResizeObserver !== 'undefined') {
        new ResizeObserver(() => GFWeb.postResize()).observe(canvas);
      } else {
        window.addEventListener('resize', () => GFWeb.postResize());
      }
      GFWeb.watchScale();
      document.addEventListener('visibilitychange', () => {
        _gf_web_post_visibility(document.hidden ? 1 : 0);
        // A frame waiting for an animation frame is presented now.
        if (document.hidden && Atomics.load(HEAPU32, GFWeb.frame >> 2) === 1) GFWeb.present();
      });
      window.addEventListener('pagehide', () => _gf_web_post_close_page());
      GFWeb.hookClose();
    },
  },

  gf_web_js_attach__deps: ['$GFWeb'],
  gf_web_js_attach__proxy: 'sync',
  gf_web_js_attach: (frame, width, height, scale) => {
    const canvas = Module['guiFormsCanvas'] || document.getElementById('gui-forms-canvas');
    if (!canvas) throw new Error('GUI.Forms: no canvas (#gui-forms-canvas or Module.guiFormsCanvas)');
    GFWeb.canvas = canvas;
    GFWeb.frame = frame;
    GFWeb.setUpGl();
    GFWeb.listen();
    const box = canvas.getBoundingClientRect();
    HEAPF64[width >> 3] = Math.max(1, box.width);
    HEAPF64[height >> 3] = Math.max(1, box.height);
    HEAPF64[scale >> 3] = window.devicePixelRatio || 1;
    if (document.hasFocus()) GFWeb.input.focus({ preventScroll: true });
  },

  gf_web_js_request_present__deps: ['$GFWeb'],
  gf_web_js_request_present__proxy: 'async',
  gf_web_js_request_present: () => GFWeb.schedule(),

  gf_web_js_want_frames__deps: ['$GFWeb'],
  gf_web_js_want_frames__proxy: 'async',
  gf_web_js_want_frames: (wanted) => {
    GFWeb.framesWanted = wanted !== 0;
    if (GFWeb.framesWanted) GFWeb.schedule();
  },

  gf_web_js_set_title__deps: ['$UTF8ToString'],
  gf_web_js_set_title__proxy: 'sync',
  gf_web_js_set_title: (title) => { document.title = UTF8ToString(title); },

  gf_web_js_set_cursor__deps: ['$GFWeb'],
  gf_web_js_set_cursor__proxy: 'async',
  gf_web_js_set_cursor: (kind, hidden) => {
    // gui_forms::CursorKind, in order.
    const names = ['default', 'text', 'pointer', 'crosshair', 'ew-resize', 'ns-resize', 'wait', 'not-allowed',
                   'nwse-resize', 'nesw-resize'];
    const name = hidden ? 'none' : (names[kind] || 'default');
    if (GFWeb.cursor === name) return;
    GFWeb.cursor = name;
    GFWeb.canvas.style.cursor = name;
  },

  gf_web_js_pointer_capture__deps: ['$GFWeb'],
  gf_web_js_pointer_capture__proxy: 'async',
  gf_web_js_pointer_capture: (captured, pointer) => {
    // The canvas captures every press (listen); a release lets go.
    if (captured) return;
    try {
      if (GFWeb.canvas.hasPointerCapture(pointer)) GFWeb.canvas.releasePointerCapture(pointer);
    } catch (error) {
    }
  },

  gf_web_js_write_clipboard__deps: ['$GFWeb', '$UTF8ToString'],
  gf_web_js_write_clipboard__proxy: 'sync',
  gf_web_js_write_clipboard: (text) => {
    const value = UTF8ToString(text);
    GFWeb.clipboard = value;
    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(value).catch((error) => {
        console.warn('GUI.Forms: the browser refused the clipboard write; the copy stays within the page', error);
      });
    }
  },

  gf_web_js_place_input__deps: ['$GFWeb'],
  gf_web_js_place_input__proxy: 'async',
  gf_web_js_place_input: (x, y, width, height, editing) => {
    const input = GFWeb.input;
    if (!input) return;
    const canvas = GFWeb.canvas;
    input.style.left = (canvas.offsetLeft + x) + 'px';
    input.style.top = (canvas.offsetTop + y) + 'px';
    input.style.height = Math.max(1, height) + 'px';
    input.setAttribute('inputmode', editing ? 'text' : 'none');
  },

  gf_web_js_full_screen__deps: ['$GFWeb'],
  gf_web_js_full_screen__proxy: 'async',
  gf_web_js_full_screen: () => {
    const target = GFWeb.canvas.parentElement || GFWeb.canvas;
    if (document.fullscreenElement) {
      document.exitFullscreen().catch(() => {});
    } else if (target.requestFullscreen) {
      target.requestFullscreen().catch(() => {});
    }
  },

  gf_web_js_open_files__deps: ['$GFWeb', '$UTF8ToString'],
  gf_web_js_open_files__proxy: 'sync',
  gf_web_js_open_files: (request, accept, multiple) => {
    const filter = UTF8ToString(accept);
    const start = () => {
      const chooser = document.createElement('input');
      chooser.type = 'file';
      if (filter) chooser.accept = filter;
      chooser.multiple = multiple !== 0;
      let answered = false;
      chooser.addEventListener('change', async () => {
        if (answered) return;
        answered = true;
        try {
          const paths = await GFWeb.writeFiles(chooser.files, '/web/files/' + request);
          GFWeb.postDialog(request, paths.length > 0 ? 1 : 0, paths, '');
        } catch (error) {
          console.error('GUI.Forms: reading the chosen files failed', error);
          GFWeb.postDialog(request, 2, [], '');
        }
      });
      chooser.addEventListener('cancel', () => {
        if (answered) return;
        answered = true;
        GFWeb.postDialog(request, 0, [], '');
      });
      chooser.click();
    };
    GFWeb.withActivation('Choose file…', start, () => GFWeb.postDialog(request, 0, [], ''));
  },

  gf_web_js_save_file__deps: ['$GFWeb', '$UTF8ToString'],
  gf_web_js_save_file__proxy: 'sync',
  gf_web_js_save_file: (request, path, name, accept) => {
    const target = UTF8ToString(path);
    const suggested = UTF8ToString(name);
    const filter = UTF8ToString(accept);
    if (!window.showSaveFilePicker) {
      // A download: delivered when the program closes the file.
      GFWeb.saves.set(target, { name: suggested, handle: null });
      GFWeb.postDialog(request, 1, [target], '');
      return;
    }
    const start = () => {
      const options = { suggestedName: suggested };
      if (filter) {
        const extensions = filter.split(',');
        options.types = [{ description: 'Files', accept: { 'application/octet-stream': extensions } }];
      }
      window.showSaveFilePicker(options).then((handle) => {
        GFWeb.saves.set(target, { name: suggested, handle: handle });
        GFWeb.postDialog(request, 1, [target], '');
      }, (error) => {
        GFWeb.postDialog(request, error && error.name === 'AbortError' ? 0 : 2, [], '');
      });
    };
    GFWeb.withActivation('Save…', start, () => GFWeb.postDialog(request, 0, [], ''));
  },

  gf_web_js_choose_color__deps: ['$GFWeb'],
  gf_web_js_choose_color__proxy: 'sync',
  gf_web_js_choose_color: (request, rgb) => {
    const start = () => {
      const chooser = document.createElement('input');
      chooser.type = 'color';
      chooser.value = '#' + (rgb >>> 0).toString(16).padStart(6, '0').slice(-6);
      let answered = false;
      chooser.addEventListener('change', () => {
        if (answered) return;
        answered = true;
        GFWeb.postDialog(request, 1, [], chooser.value);
      });
      chooser.addEventListener('cancel', () => {
        if (answered) return;
        answered = true;
        GFWeb.postDialog(request, 0, [], '');
      });
      chooser.click();
    };
    GFWeb.withActivation('Choose colour…', start, () => GFWeb.postDialog(request, 0, [], ''));
  },
});
