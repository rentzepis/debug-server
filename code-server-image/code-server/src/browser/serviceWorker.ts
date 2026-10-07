self.addEventListener("install", () => {
  console.debug("[Service Worker] installed")
})

self.addEventListener("activate", (event: any) => {
  event.waitUntil((self as any).clients.claim())
  console.debug("[Service Worker] activated")
})

// No fetch handler: Chrome requires one to treat the site as an installable PWA.
