// Service worker used by tests/fp-probe.html to read the worker-side navigator.
//
// A service worker runs in its own context with its own navigator object.
// DevTools/CDP based User-Agent overrides cannot reach it, so a browser that
// only overrides the main thread will report a different identity here — which
// is exactly the contradiction detectors look for.
//
// This file only reports values; it does not modify fetches or cache anything
// it is not asked to.

self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', (event) => event.waitUntil(self.clients.claim()));

self.addEventListener('message', (event) => {
  if (!event.data || event.data.kind !== 'probe') return;
  const port = event.ports[0];
  if (!port) return;
  const n = self.navigator;
  port.postMessage({
    userAgent: n.userAgent,
    appVersion: n.appVersion,
    platform: n.platform,
    hardwareConcurrency: n.hardwareConcurrency,
    deviceMemory: n.deviceMemory ?? null,
    language: n.language,
    languages: n.languages,
    vendor: n.vendor,
    productSub: n.productSub,
    webdriver: n.webdriver,
    uaBrands: n.userAgentData
        ? n.userAgentData.brands.map((b) => [b.brand, b.version]) : null,
  });
});
