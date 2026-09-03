# Firefox long-polling investigation

The RFStatus dashboard uses 18 subscriptions at approximately 20 updates/s, so the browser must complete about **360
long-poll requests per second**. These measurements used the normal Emscripten XHR backend, without
`-sFETCH_STREAMING=2` and without `Cache-Control: no-store`.

| Browser and configuration       | Dashboard inactive | Dashboard active      |
| ------------------------------- | ------------------ | --------------------- |
| Firefox, browser cache disabled | Stable 360/s       | 330–360/s, near limit |
| Firefox, browser cache enabled  | 195–200/s          | 170–180/s             |
| Chrome, one dashboard           | Stable 360/s       | Stable 360/s          |

With Firefox caching disabled, two hidden dashboard windows each reached approximately 345/s. This shows that neither
the server nor OpenCMW has a fixed total limit of 360/s.

Chrome also handled one dashboard at the full rate regardless of whether it was active or hidden. With three Chrome
dashboards open simultaneously, each still reached about 270/s and showed no active/inactive difference. This emphasizes
that the single-dashboard problem and its rendering dependency are specific to Firefox in these measurements.

## Conclusions

- Firefox's HTTP cache processing is the largest bottleneck.
- Active ImGui/WebGL rendering causes an additional slowdown and can leave Firefox close to, or slightly below, the
  required rate.
- Firefox implements worker XHR by synchronously forwarding XHR control operations to the browser's main thread. The
  network transfer still runs on networking threads, but starting and rearming requests can wait behind rendering and
  other browser work. Activating and hiding dashboard tabs makes this contention visible, especially with two Firefox
  windows: throughput drops while the dashboards are rendered and recovers when they are hidden. See Firefox's
  [worker-XHR implementation](https://searchfox.org/firefox-main/source/dom/xhr/XMLHttpRequestWorker.cpp).
- Chrome does not show the same active/inactive-dashboard dependency, indicating that it handles this workload more
  independently from rendering.
- OpenDigitizer callback processing is not the bottleneck. Deserialisation, callback execution and queue locking were
  measured in hundredths of a millisecond, with a queue depth of 0–1.

The OpenCMW server retains 100 responses per subscription. At 20 updates/s, this is approximately five seconds of
history. When Firefox remains below the required 360/s, it gradually falls behind. Once its lag exceeds the retained
history, OpenCMW skips forward and the missing interval appears as a five-second gap. With browser caching enabled,
this happens much faster.

## Other tests

- A standalone JavaScript test also fell below the required rate in Firefox, without OpenDigitizer, WebAssembly, or
  plotting.
- A second OpenCMW fetch worker divided the subscriptions but did not improve their combined throughput.
- `Cache-Control: no-store` helped, but was not as consistently effective as disabling the Firefox caches.
- `-sFETCH_STREAMING=2` reduced request submission time, but did not reliably restore the required throughput.

## Current workaround

For a dedicated Firefox profile, set the following preferences to `false` in `about:config`:

```text
browser.cache.disk.enable = false
browser.cache.memory.enable = false
```
