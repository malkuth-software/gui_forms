// Cross-origin isolation for hosts that drop COOP and COEP (first-party;
// shell.js registers it only when the page is not isolated). Every response
// this worker passes to the page carries the isolation headers, and
// same-origin resources the cross-origin resource policy, so the page
// becomes crossOriginIsolated after one reload.
'use strict';

self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', (event) => event.waitUntil(self.clients.claim()));

function isolated(response) {
  // Opaque responses (no-cors) cannot be read or re-wrapped.
  if (response.status === 0) return response;
  const headers = new Headers(response.headers);
  headers.set('Cross-Origin-Opener-Policy', 'same-origin');
  headers.set('Cross-Origin-Embedder-Policy', 'require-corp');
  if (!headers.has('Cross-Origin-Resource-Policy')) headers.set('Cross-Origin-Resource-Policy', 'same-origin');
  return new Response(response.body, { status: response.status, statusText: response.statusText, headers: headers });
}

self.addEventListener('fetch', (event) => {
  const request = event.request;
  // A cached-only request outside same-origin mode throws on fetch.
  if (request.cache === 'only-if-cached' && request.mode !== 'same-origin') return;
  event.respondWith(fetch(request).then(isolated, (error) => {
    console.error('GUI.Forms isolation worker: fetch failed', error);
    return Response.error();
  }));
});
