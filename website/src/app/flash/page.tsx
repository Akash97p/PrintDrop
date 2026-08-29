"use client";

import { useState, useEffect, useCallback, useRef } from "react";
import Link from "next/link";
import {
  AlertTriangle,
  Cable,
  Cpu,
  Download,
  ExternalLink,
  HardDrive,
  RefreshCw,
  ShieldCheck,
  Terminal,
  Usb,
  Wifi,
  Zap,
} from "lucide-react";

import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
  CardTitle,
} from "@/components/ui/card";
import { Separator } from "@/components/ui/separator";

const GITHUB_REPO = "Akash97p/PrintDrop";
const GITHUB_RELEASES = `https://github.com/${GITHUB_REPO}/releases`;
const GITHUB_API_LATEST = `https://api.github.com/repos/${GITHUB_REPO}/releases/latest`;

type ReleaseAsset = {
  name: string;
  browser_download_url: string;
  size: number;
};
type ReleaseInfo = {
  tag_name: string;
  name: string;
  published_at: string;
  html_url: string;
  assets: ReleaseAsset[];
};

function fmtBytes(n: number) {
  if (!n) return "—";
  const u = ["B", "KB", "MB", "GB"];
  let i = 0;
  let v = n;
  while (v >= 1024 && i < u.length - 1) {
    v /= 1024;
    i++;
  }
  return (v >= 10 ? Math.round(v) : Math.round(v * 10) / 10) + " " + u[i];
}

export default function FlashPage() {
  const [release, setRelease] = useState<ReleaseInfo | null>(null);
  const [releaseError, setReleaseError] = useState<string | null>(null);
  const [variant, setVariant] = useState<"sdio" | "spi">("sdio");
  const [file, setFile] = useState<File | null>(null);
  const [log, setLog] = useState<string[]>([]);
  const [flashing, setFlashing] = useState(false);
  const [progress, setProgress] = useState(0);
  const [serialSupported, setSerialSupported] = useState<boolean | null>(null);
  const fileInputRef = useRef<HTMLInputElement>(null);
  const logRef = useRef<HTMLPreElement>(null);

  useEffect(() => {
    setSerialSupported(typeof navigator !== "undefined" && "serial" in navigator);
  }, []);

  const addLog = useCallback((s: string) => {
    setLog((p) => [...p.slice(-80), `${new Date().toLocaleTimeString()} ${s}`]);
  }, []);

  useEffect(() => {
    if (logRef.current) logRef.current.scrollTop = logRef.current.scrollHeight;
  }, [log]);

  useEffect(() => {
    let cancelled = false;
    async function fetchRelease() {
      try {
        const res = await fetch(GITHUB_API_LATEST, { cache: "no-store" });
        if (!res.ok) throw new Error(`GitHub API ${res.status}`);
        const j = (await res.json()) as ReleaseInfo;
        if (!cancelled) setRelease(j);
      } catch (e) {
        if (!cancelled) setReleaseError(e instanceof Error ? e.message : String(e));
      }
    }
    fetchRelease();
    return () => {
      cancelled = true;
    };
  }, []);

  const sdioFullAsset = release?.assets.find((a) => a.name.startsWith("printdrop-sdio-full-") && a.name.endsWith(".bin"));
  const spiFullAsset = release?.assets.find((a) => a.name.startsWith("printdrop-spi-full-") && a.name.endsWith(".bin"));
  const sdioOtaAsset = release?.assets.find((a) => /^printdrop-sdio-.*\.bin$/.test(a.name) && !a.name.includes("full"));
  const spiOtaAsset = release?.assets.find((a) => /^printdrop-spi-.*\.bin$/.test(a.name) && !a.name.includes("full"));
  const currentFullUrl = variant === "sdio" ? sdioFullAsset?.browser_download_url : spiFullAsset?.browser_download_url;
  const currentOtaUrl = variant === "sdio" ? sdioOtaAsset?.browser_download_url : spiOtaAsset?.browser_download_url;
  const variantFullAsset = variant === "sdio" ? sdioFullAsset : spiFullAsset;
  const variantOtaAsset = variant === "sdio" ? sdioOtaAsset : spiOtaAsset;
  const cleanVer = release?.tag_name?.replace(/^v/, "") || "0.2.1";
  const jsonVersion = variant === "spi" ? `${cleanVer}-spi` : cleanVer;

  const handleDownloadJson = useCallback(() => {
    const json = JSON.stringify({ version: jsonVersion }, null, 2);
    const blob = new Blob([json], { type: "application/json" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = "firmware.json";
    a.click();
    setTimeout(() => URL.revokeObjectURL(url), 2000);
  }, [jsonVersion]);

  const handleFileChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    const f = e.target.files?.[0] || null;
    setFile(f);
    if (f) addLog(`selected ${f.name} (${fmtBytes(f.size)})`);
  };

  const handleDownloadFromRelease = useCallback(
    async (useOta: boolean) => {
      const url = useOta ? currentOtaUrl : currentFullUrl;
      if (!url) {
        addLog("no release asset for this variant yet — use manual esptool or wait for CI");
        return;
      }
      addLog(`fetching ${url} ...`);
      try {
        const r = await fetch(url);
        if (!r.ok) throw new Error(`fetch ${r.status}`);
        const blob = await r.blob();
        const f = new File([blob], url.split("/").pop() || (useOta ? "firmware.bin" : "full.bin"), { type: "application/octet-stream" });
        setFile(f);
        addLog(`downloaded ${f.name} (${fmtBytes(f.size)}) — ready to flash at 0x0 (merged)`);
      } catch (e) {
        addLog(`download failed: ${e instanceof Error ? e.message : String(e)} — try direct download link below`);
      }
    },
    [currentFullUrl, currentOtaUrl, addLog]
  );

  const handleFlash = useCallback(async () => {
    if (!file) {
      addLog("choose a .bin first (merged full image recommended for initial flash)");
      return;
    }
    if (!serialSupported) {
      addLog("Web Serial not supported — use Chrome/Edge on desktop, or esptool.py");
      return;
    }
    setFlashing(true);
    setProgress(0);
    addLog(`opening serial port …`);
    try {
      // dynamic import so SSR doesn't pull Web Serial
      const { ESPLoader, Transport } = await import("esptool-js");
      // @ts-expect-error navigator.serial is not in lib.dom yet
      const port = await navigator.serial.requestPort();
      const transport = new Transport(port, true);
      const terminal: unknown = {
        clean: () => {},
        writeLine: (data: string) => addLog(data),
        write: (data: string) => addLog(data),
      };
      // esptool-js LoaderOptions shape varies between 0.5.x and 0.6.x — cast to any to stay compat
      const loader = new (ESPLoader as unknown as { new (opts: unknown): { main: () => Promise<string>; writeFlash: (opts: unknown) => Promise<void>; after: (mode?: string) => Promise<void> } })({
        transport: transport as unknown,
        baudrate: 115200,
        terminal: terminal as unknown,
        romBaudrate: 115200,
      } as unknown);
      addLog(`connecting… (hold BOOT, tap RESET if needed; or send BOOTLOADER via serial)`);
      const chip = await loader.main();
      addLog(`chip: ${chip} — erasing & flashing at 0x0`);
      const data = await file.arrayBuffer();
      // esptool-js writeFlash: fileArray [{data: string|Uint8Array, address}]
      await (loader as unknown as { writeFlash: (o: unknown) => Promise<void> }).writeFlash({
        fileArray: [{ data: new Uint8Array(data) as unknown as string, address: 0x0 }],
        flashSize: "keep",
        eraseAll: false,
        compress: true,
        reportProgress: (fileIndex: number, written: number, total: number) => {
          const pct = Math.round((written / total) * 100);
          setProgress(pct);
        },
        calculateMD5Hash: () => "",
      } as unknown);
      addLog(`flash done — resetting`);
      await loader.after();
      try {
        await transport.disconnect();
      } catch {}
      addLog(`✓ flashed ${file.name} — device will reboot as PrintDrop`);
      setProgress(100);
    } catch (e) {
      const msg = e instanceof Error ? e.message : String(e);
      addLog(`✗ flash failed: ${msg}`);
      // common hint
      if (msg.includes("Failed to open serial port") || msg.includes("requestPort")) {
        addLog(`hint: close Arduino Monitor / VS Code serial on COM5/COM11 first — RTS resets the chip`);
      }
    } finally {
      setFlashing(false);
    }
  }, [file, serialSupported, addLog]);

  return (
    <div className="mx-auto max-w-7xl px-4 py-10 sm:px-6">
      <div className="max-w-3xl">
        <Badge variant="secondary">Flash Firmware</Badge>
        <h1 className="mt-4 text-4xl font-semibold tracking-[-0.04em]">Flash PrintDrop</h1>
        <p className="mt-4 leading-7 text-muted-foreground">
          Two paths: the easy OTA update from the device&apos;s own web UI, and the full Web Serial flasher for first
          install or recovery. Both use the same binaries built by CI for <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">printdrop</code>{" "}
          (SDIO 4-bit) and{" "}
          <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">printdrop_spi</code> (SPI legacy) — dual OTA slots on{" "}
          <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">4 MB</code>.
        </p>
      </div>

      {/* OTA */}
      <Card className="mt-8">
        <CardHeader>
          <CardTitle className="flex items-center gap-2">
            <Wifi className="size-4" /> Recommended — OTA from the device
          </CardTitle>
          <CardDescription>
            No cable. Works once the device is on your Wi-Fi. Uses dual 1 344 KB OTA slots — a bad flash rolls back.
          </CardDescription>
        </CardHeader>
        <CardContent className="grid gap-4">
          <ol className="list-decimal space-y-2 pl-5 text-sm leading-6">
            <li>
              Open the device:{" "}
              <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">http://printdrop.local</code> or{" "}
              <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">http://printdrop/</code> or its IP (
              see <Link href="/devices" className="underline-offset-4 hover:underline">Devices</Link> scanner). Log in with
              your Web UI login (default <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">admin / printdrop</code>).
            </li>
            <li>
              Go to <strong>Settings → Firmware (OTA)</strong> — the three Settings tabs are now Wi-Fi / Web UI Login /
              Firmware (OTA). The card shows Current version and OTA enabled.
            </li>
            <li>
              Click <strong>Choose firmware .bin</strong>, pick the OTA binary for your wiring:{" "}
              <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">printdrop-sdio-*.bin</code> for SDIO 4-bit
              (default) or{" "}
              <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">printdrop-spi-*.bin</code> for SPI legacy. Confirm — the
              device writes the idle OTA slot and reboots.
            </li>
            <li>
              Alternative: copy{" "}
              <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">firmware.bin</code> +{" "}
              <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">firmware.json</code>{" "}
              <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">{`{"version":"0.2.0"}`}</code> to SD root, then
              refresh — OTA will offer &quot;Update from SD&quot;.
            </li>
          </ol>
          <div className="flex flex-wrap gap-2">
            <Button asChild>
              <a href={currentOtaUrl || `${GITHUB_RELEASES}/latest`} target="_blank" rel="noopener">
                <Download className="size-4" /> Download OTA bin {variant === "spi" ? "SPI" : "SDIO"} {release?.tag_name ? `(${release.tag_name})` : ""}
              </a>
            </Button>
            <Button variant="outline" onClick={handleDownloadJson}>
              <Download className="size-3" /> Download firmware.json <span className="font-mono text-xs">({`{"version":"${jsonVersion}"}`})</span>
            </Button>
            <Button variant="outline" asChild>
              <a href={`${GITHUB_RELEASES}`} target="_blank" rel="noopener">
                All releases <ExternalLink className="size-3" />
              </a>
            </Button>
          </div>
          <p className="text-xs text-muted-foreground">OTA via web UI needs only the <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">.bin</code>. For SD-card update copy both <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">firmware.bin</code> (rename the OTA bin) + <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">firmware.json</code> to SD root — the json is generated for the selected variant ({variant === "spi" ? "SPI" : "SDIO"}).</p>
          <p className="text-xs text-muted-foreground">
            LED: idle 2 s blink, activity fast, error double-blink. Button short = eject/refresh printer view; long 5 s =
            factory reset.
          </p>
        </CardContent>
      </Card>

      {/* Web Serial */}
      <Card className="mt-6">
        <CardHeader>
          <CardTitle className="flex items-center gap-2">
            <Cable className="size-4" /> Web Serial flasher — esptool-js
          </CardTitle>
          <CardDescription>
            First install, or recovery when OTA is not reachable. Runs in Chrome/Edge via Web Serial — no Python needed.
            The same merged binaries as CI builds are flashed at{" "}
            <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">0x0</code>.
          </CardDescription>
        </CardHeader>
        <CardContent className="grid gap-4">
          {!serialSupported && (
            <div className="flex gap-2 rounded-lg border border-amber-200 bg-amber-50 p-3 text-sm text-amber-900 dark:border-amber-900 dark:bg-amber-950/30 dark:text-amber-100">
              <AlertTriangle className="mt-0.5 size-4 shrink-0" />
              <span>
                Web Serial is not available here (needs Chrome or Edge on desktop over https or localhost). You can still
                download the bins and use <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">esptool.py</code> below.
              </span>
            </div>
          )}

          <div className="flex flex-wrap items-center gap-3">
            <span className="text-sm font-medium">Variant:</span>
            <div className="inline-flex rounded-md border p-1">
              <button
                type="button"
                onClick={() => setVariant("sdio")}
                className={`rounded px-3 py-1.5 text-sm font-medium transition-colors ${variant === "sdio" ? "bg-primary text-primary-foreground" : "text-muted-foreground hover:text-foreground"}`}
              >
                SDIO 4-bit (default)
              </button>
              <button
                type="button"
                onClick={() => setVariant("spi")}
                className={`rounded px-3 py-1.5 text-sm font-medium transition-colors ${variant === "spi" ? "bg-primary text-primary-foreground" : "text-muted-foreground hover:text-foreground"}`}
              >
                SPI legacy
              </button>
            </div>
            <span className="text-xs text-muted-foreground">
              SDIO needs 6 wires (CLK 40 CMD 14 D0 39 D1 12 D2 13 D3 15) + 10 k pull-ups; SPI is 4-wire CS 12 MISO 39 MOSI 14 CLK 40.
            </span>
          </div>

          <div className="grid gap-2 rounded-lg border bg-muted/30 p-4">
            <div className="text-sm font-medium">Latest release — {variant === "spi" ? "SPI legacy" : "SDIO 4-bit"} {release?.tag_name ? `· ${release.tag_name}` : ""}</div>
            {release ? (
              <div className="grid gap-2 text-sm">
                <div className="font-mono text-xs">
                  <a href={release.html_url} target="_blank" rel="noopener" className="underline-offset-4 hover:underline">
                    {release.tag_name} — {release.name}
                  </a>{" "}
                  · {new Date(release.published_at).toLocaleString()} · {release.assets.length} assets total
                </div>
                <div className="flex flex-wrap gap-2">
                  {variantOtaAsset ? (
                    <a href={variantOtaAsset.browser_download_url} className="inline-flex items-center gap-1 rounded-md border bg-background px-2.5 py-1.5 text-xs hover:bg-accent">
                      <Download className="size-3" /> {variant === "spi" ? "SPI" : "SDIO"} OTA {fmtBytes(variantOtaAsset.size)}
                    </a>
                  ) : (
                    <span className="inline-flex items-center gap-1 rounded-md border bg-muted px-2.5 py-1.5 text-xs text-muted-foreground">{variant === "spi" ? "SPI" : "SDIO"} OTA — pending for this release</span>
                  )}
                  {variantFullAsset ? (
                    <a href={variantFullAsset.browser_download_url} className="inline-flex items-center gap-1 rounded-md border bg-background px-2.5 py-1.5 text-xs hover:bg-accent">
                      <Download className="size-3" /> {variant === "spi" ? "SPI" : "SDIO"} full {fmtBytes(variantFullAsset.size)}
                    </a>
                  ) : (
                    <span className="inline-flex items-center gap-1 rounded-md border bg-muted px-2.5 py-1.5 text-xs text-muted-foreground">{variant === "spi" ? "SPI" : "SDIO"} full — pending for this release (use OTA or wait for v0.2.1+)</span>
                  )}
                </div>
                <div className="text-xs text-muted-foreground">Filtered to selected variant. Full = bootloader + partitions + app + littlefs merged at 0x0 (flash once). OTA = app only for Settings → Firmware (OTA). Switch variant above to see the other pair · <a href={`${GITHUB_RELEASES}/tag/${release.tag_name}`} target="_blank" rel="noopener" className="underline">View all 8 assets on GitHub</a>.</div>
              </div>
            ) : releaseError ? (
              <div className="text-sm text-muted-foreground">
                Could not load release list: {releaseError}. Browse{" "}
                <a href={GITHUB_RELEASES} target="_blank" rel="noopener" className="underline">
                  Releases
                </a>{" "}
                directly.
              </div>
            ) : (
              <div className="text-sm text-muted-foreground">Loading latest release…</div>
            )}
          </div>

          <div className="grid gap-3 rounded-lg border p-4">
            <div className="flex flex-wrap items-end gap-3">
              <label className="grid gap-1 text-sm">
                <span className="font-medium">Firmware .bin (merged full recommended)</span>
                <input
                  ref={fileInputRef}
                  type="file"
                  accept=".bin"
                  onChange={handleFileChange}
                  className="rounded-md border bg-background px-3 py-2 text-sm file:mr-3 file:rounded file:border-0 file:bg-primary file:px-3 file:py-1.5 file:text-sm file:font-medium file:text-primary-foreground hover:file:bg-primary/90"
                />
              </label>
              {currentFullUrl ? (
                <Button variant="outline" asChild>
                  <a href={currentFullUrl} target="_blank" rel="noopener">
                    <Download className="size-3" /> Download merged
                  </a>
                </Button>
              ) : (
                <Button variant="outline" disabled>
                  Download merged (pending)
                </Button>
              )}
              {currentOtaUrl ? (
                <Button variant="outline" asChild>
                  <a href={currentOtaUrl} target="_blank" rel="noopener">
                    <Download className="size-3" /> Download OTA
                  </a>
                </Button>
              ) : (
                <Button variant="outline" disabled>
                  Download OTA (pending)
                </Button>
              )}
            </div>
            <p className="text-xs text-muted-foreground">Download from GitHub Releases, then pick the file above — direct browser fetch from GitHub is blocked by CORS, so download + select is the reliable path.</p>
            {file && <div className="text-xs text-muted-foreground">Ready: {file.name} — {fmtBytes(file.size)} — will flash at 0x0 (merged)</div>}

            <div className="flex flex-wrap gap-2">
              <Button onClick={handleFlash} disabled={flashing || !file || !serialSupported}>
                <Zap className="size-4" /> {flashing ? `Flashing ${progress}%…` : "Connect & Flash via Web Serial"}
              </Button>
              <Button variant="outline" disabled={flashing} onClick={() => setLog([])}>
                Clear log
              </Button>
              <span className="inline-flex items-center gap-1.5 text-xs text-muted-foreground">
                <Usb className="size-3" /> Needs Chrome/Edge, USB cable to CP2102 (COM5) or native USB — may need BOOT+RESET
              </span>
            </div>

            {flashing && (
              <div className="h-2 overflow-hidden rounded-full bg-muted">
                <div className="h-full bg-primary transition-all" style={{ width: `${progress}%` }} />
              </div>
            )}

            <pre ref={logRef} className="max-h-48 overflow-auto whitespace-pre-wrap break-words rounded-md bg-black p-3 font-mono text-xs leading-5 text-zinc-300">
              {log.join("\n") || "— idle — connect a board, pick a .bin, then flash —\nTip: if the board runs TinyUSB (printdrop), send BOOTLOADER via serial first: \"BOOTLOADER\" | Out-File -Encoding ascii COM5"}
            </pre>
            <p className="text-xs text-muted-foreground">
              If Web Serial fails, use <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">esptool.py</code> — see command below. The auto-reset on this board is half-wired (RTS→EN only); open the port with RTS/DTR deasserted.
            </p>
          </div>
        </CardContent>
      </Card>

      {/* Manual esptool */}
      <Card className="mt-6">
        <CardHeader>
          <CardTitle className="flex items-center gap-2">
            <Terminal className="size-4" /> Manual — esptool.py
          </CardTitle>
          <CardDescription>When the browser route is not available, or for CI.</CardDescription>
        </CardHeader>
        <CardContent className="grid gap-4">
          <div>
            <div className="text-sm font-medium">OTA via curl (device already on Wi-Fi)</div>
            <pre className="mt-2 overflow-auto rounded-lg border bg-muted p-3 font-mono text-xs">
              {`# needs Web UI login (default admin/printdrop)
curl -u admin:printdrop -F "file=@printdrop-sdio-${release?.tag_name?.replace(/^v/, "") || "0.2.0"}.bin" http://printdrop.local/api/ota
# — device writes idle OTA slot and reboots`}
            </pre>
          </div>
          <div>
            <div className="text-sm font-medium">Full flash via esptool (initial install)</div>
            <pre className="mt-2 overflow-auto rounded-lg border bg-muted p-3 font-mono text-xs">
              {`# put board in download mode first:
#  - software: "BOOTLOADER" | Out-File -Encoding ascii COM5   (then check for "waiting for download")
#  - hardware: hold BOOT, tap RESET, release BOOT
esptool.py --chip esp32s3 --port COM11 --before default_reset --after hard_reset write_flash -z \\
  0x0      bootloader-${release?.tag_name?.replace(/^v/, "") || "0.2.0"}.bin \\
  0x8000   partitions-${release?.tag_name?.replace(/^v/, "") || "0.2.0"}.bin \\
  0x10000  printdrop-sdio-${release?.tag_name?.replace(/^v/, "") || "0.2.0"}.bin \\
  0x2B0000 littlefs-${release?.tag_name?.replace(/^v/, "") || "0.2.0"}.bin
# or single shot with merged full image:
esptool.py --chip esp32s3 --port COM11 write_flash -z 0x0 printdrop-sdio-full-${release?.tag_name?.replace(/^v/, "") || "0.2.0"}.bin`}
            </pre>
            <p className="mt-2 text-xs text-muted-foreground">
              Offsets are from <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">partitions_printdrop_ota.csv</code> (4 MB: app0 0x10000 0x150000, app1 0x160000, spiffs 0x2B0000). For SPI build
              replace{" "}
              <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">sdio</code> with{" "}
              <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">spi</code>. If{" "}
              <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">COM11</code> is gone (TinyUSB firmware),
              use the BOOTLOADER hatch on <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">COM5</code>.
            </p>
          </div>
          <div className="grid gap-2 rounded-lg border bg-card p-3 text-sm">
            <div className="flex items-center gap-2 font-medium">
              <HardDrive className="size-4" /> LittleFS
            </div>
            <p className="leading-6 text-muted-foreground">
              The Web UI (HTML/CSS/JS) lives in LittleFS (<code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">data/</code>). After a full
              flash that includes <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">littlefs.bin</code>, no extra step is needed. To update
              only the UI:{" "}
              <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">pio run -e printdrop -t uploadfs</code> (or flash{" "}
              <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">littlefs.bin</code> at <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">0x2B0000</code>).
            </p>
          </div>
        </CardContent>
      </Card>

      <Card className="mt-6 bg-muted/30">
        <CardContent className="p-4 text-sm leading-6 text-muted-foreground">
          <div className="flex items-center gap-2 font-semibold text-foreground">
            <ShieldCheck className="size-4" /> Notes
          </div>
          <ul className="mt-2 list-disc pl-5">
            <li>
              SDIO 4-bit is the default (<code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">-e printdrop</code>). SPI legacy is{" "}
              <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">-e printdrop_spi</code> — same pins plus D1 12 D2 13 D3 15, needs 10 k pull-ups on
              CMD/D0-D3.
            </li>
            <li>
              Version is <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">PRINTDROP_VERSION</code> from{" "}
              <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">platformio.ini</code> and shown in the device&apos;s Status and OTA cards
              and at <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">/api/status</code>.
            </li>
            <li>
              CI builds both envs on every tag <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">v*</code> and attaches
              bins + SHA256SUMS to the GitHub Release — the Web Serial flasher above can fetch them directly.
            </li>
          </ul>
        </CardContent>
      </Card>

      <div className="mt-6 text-sm text-muted-foreground">
        <Link href="/" className="underline-offset-4 hover:underline">
          ← Back to home
        </Link>
        {" · "}
        <Link href="/devices" className="underline-offset-4 hover:underline">
          Devices scanner
        </Link>
      </div>
    </div>
  );
}
