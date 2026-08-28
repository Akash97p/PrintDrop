"use client";

import { useState, useCallback, useEffect } from "react";
import Link from "next/link";
import {
  Activity,
  AlertTriangle,
  ExternalLink,
  HardDrive,
  Radio,
  Search,
  Wifi,
  Cpu,
  Globe,
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

type DeviceStatus = {
  name: string;
  version: string;
  hostname: string;
  ip: string;
  ssid: string;
  rssi: number;
  mode: string;
  card: {
    present: boolean;
    totalBytes: number;
    usedBytes: number;
    freeBytes: number;
    busHz: number;
    busMode: string;
    busWidth: number;
  };
  usb: { hostPresent: boolean; mediaPresent: boolean };
  discovery: { mdns: boolean; llmnr: boolean };
  auth: { required: boolean; user: string };
  wsPort: number;
  ota: { enabled: boolean; version: string };
  uptimeMs: number;
};

type FoundDevice = {
  url: string;
  status: DeviceStatus;
};

function fmtBytes(n: number) {
  if (n == null || isNaN(n)) return "—";
  if (n === 0) return "0 B";
  const u = ["B", "KB", "MB", "GB"];
  let i = 0;
  let v = n;
  while (v >= 1024 && i < u.length - 1) {
    v /= 1024;
    i++;
  }
  return (v >= 10 ? Math.round(v) : Math.round(v * 10) / 10) + " " + u[i];
}

function fmtUptime(ms: number) {
  const s = Math.floor(ms / 1000);
  const d = Math.floor(s / 86400),
    h = Math.floor((s % 86400) / 3600),
    m = Math.floor((s % 3600) / 60);
  if (d) return `${d}d ${h}h`;
  if (h) return `${h}h ${m}m`;
  if (m) return `${m}m ${s % 60}s`;
  return `${s}s`;
}

async function fetchWithTimeout(url: string, timeoutMs = 1800): Promise<DeviceStatus> {
  const ctrl = new AbortController();
  const t = setTimeout(() => ctrl.abort(), timeoutMs);
  try {
    const res = await fetch(url, {
      cache: "no-store",
      signal: ctrl.signal,
      mode: "cors",
    });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const j = (await res.json()) as DeviceStatus;
    if (!j.hostname || !j.ip) throw new Error("invalid status");
    return j;
  } finally {
    clearTimeout(t);
  }
}

export default function DevicesPage() {
  const [base, setBase] = useState("printdrop");
  const [customIp, setCustomIp] = useState("");
  const [scanning, setScanning] = useState(false);
  const [found, setFound] = useState<FoundDevice[]>([]);
  const [log, setLog] = useState<string[]>([]);
  const [isHttps, setIsHttps] = useState(false);

  useEffect(() => {
    setIsHttps(typeof window !== "undefined" && window.location.protocol === "https:");
  }, []);

  const addLog = useCallback((s: string) => setLog((p) => [...p.slice(-12), s]), []);

  const scan = useCallback(async () => {
    setScanning(true);
    setFound([]);
    setLog([]);
    const b = base.trim().toLowerCase() || "printdrop";
    const candidates: string[] = [];
    // mDNS + LLMNR for base and base-2..base-6
    const bases = [b];
    for (let i = 2; i <= 6; i++) bases.push(`${b}-${i}`);
    for (const h of bases) {
      candidates.push(`http://${h}.local/api/status`);
      candidates.push(`http://${h}/api/status`);
    }
    // also try without suffix for plain .local LLMNR already
    // custom IP if provided
    if (customIp.trim()) {
      let ip = customIp.trim();
      if (!ip.startsWith("http")) ip = `http://${ip}`;
      if (!ip.endsWith("/api/status")) ip = ip.replace(/\/$/, "") + "/api/status";
      candidates.unshift(ip);
    }
    // de-duplicate
    const uniq = [...new Set(candidates)];
    addLog(`Probing ${uniq.length} candidates…`);
    if (isHttps) {
      addLog("ℹ https→http private network — Chrome will ask “Allow local network access?” on first scan. Click Allow. Firefox/Safari: use file:// scanner.");
    }

    const results: FoundDevice[] = [];
    // probe in parallel with concurrency limit 4
    const queue = [...uniq];
    const workers = Array.from({ length: 4 }, async () => {
      while (queue.length) {
        const url = queue.shift()!;
        try {
          addLog(`try ${url}`);
          const st = await fetchWithTimeout(url, 1600);
          const devUrl = url.replace("/api/status", "");
          // dedupe by ip
          if (!results.some((r) => r.status.ip === st.ip)) {
            results.push({ url: devUrl, status: st });
            addLog(`✓ ${st.hostname}.local → ${st.ip} (${st.card.busMode} ${st.card.busWidth}-bit)`);
          }
        } catch (e: unknown) {
          const msg = e instanceof Error ? e.message : String(e);
          // AbortError is timeout / mixed content — keep quiet unless debug
          if (msg.includes("Failed to fetch") && isHttps) {
            // likely mixed content, will be reported once
          }
        }
      }
    });
    await Promise.all(workers);
    // sort by hostname
    results.sort((a, b) => a.status.hostname.localeCompare(b.status.hostname));
    setFound(results);
    if (results.length === 0) addLog("No devices answered. Try adding IP manually or check that device and PC are on same Wi-Fi.");
    else addLog(`Done — ${results.length} device(s) found.`);
    setScanning(false);
  }, [base, customIp, addLog, isHttps]);

  const downloadStandalone = useCallback(() => {
    const html = `<!doctype html><meta charset=utf-8><title>PrintDrop Scanner (standalone)</title>
<style>body{font-family:system-ui,sans-serif;max-width:780px;margin:32px auto;padding:0 16px}input{padding:8px;border:1px solid #ccc;border-radius:6px;width:220px}button{padding:8px 14px;border-radius:6px;border:1px solid #111;background:#111;color:#fff;cursor:pointer}pre{white-space:pre-wrap;background:#f5f5f5;padding:12px;border-radius:8px;font-size:12px}.card{border:1px solid #e5e5e5;border-radius:12px;padding:16px;margin:12px 0}</style>
<h1>PrintDrop — local scanner (standalone)</h1>
<p>Open this file via <code>file://</code> (double-click) — then it can fetch <code>http://printdrop.local</code> without mixed-content block.</p>
<div><input id=base value=printdrop> <button onclick=scan()>Scan</button> <input id=ip placeholder="or 10.74.179.223" style="width:160px"> <button onclick=addIp()>Add IP</button></div>
<pre id=log></pre><div id=out></div>
<script>
function fmtBytes(n){if(!n&&n!==0)return'—';if(n===0)return'0 B';const u=['B','KB','MB','GB'];let i=0,v=n;while(v>=1024&&i<u.length-1){v/=1024;i++;}return (v>=10?Math.round(v):Math.round(v*10)/10)+' '+u[i];}
async function tryOne(url, timeout=1500){
  const c=new AbortController();const t=setTimeout(()=>c.abort(),timeout);
  try{const r=await fetch(url,{cache:'no-store',signal:c.signal,mode:'cors'});if(!r.ok)throw new Error('HTTP '+r.status);return await r.json();}finally{clearTimeout(t);}
}
async function scan(){
  const base=document.getElementById('base').value.trim().toLowerCase()||'printdrop';
  const log=document.getElementById('log');const out=document.getElementById('out');log.textContent='';out.innerHTML='';
  const cands=[];for(let i=1;i<=6;i++){const h=i===1?base:base+'-'+i;cands.push('http://'+h+'.local/api/status');cands.push('http://'+h+'/api/status');}
  const ip=document.getElementById('ip').value.trim();if(ip){let u=ip;if(!u.startsWith('http'))u='http://'+u;if(!u.endsWith('/api/status'))u=u.replace(/\\/$/,'')+'/api/status';cands.unshift(u);}
  log.textContent='Probing '+cands.length+'…\\n';
  const found=[];
  await Promise.all(cands.map(async url=>{
    try{log.textContent+='try '+url+'\\n';const j=await tryOne(url);const devUrl=url.replace('/api/status','');if(!found.some(f=>f.ip===j.ip)){found.push(j);const d=document.createElement('div');d.className='card';d.innerHTML='<b>'+j.hostname+'</b> — '+j.ip+' — '+j.card.busMode+' '+j.card.busWidth+'-bit @ '+(j.card.busHz/1e6)+'MHz<br>Card '+fmtBytes(j.card.freeBytes)+' free of '+fmtBytes(j.card.totalBytes)+' — up '+Math.round(j.uptimeMs/1000)+'s<br><a href=\"'+devUrl+'\" target=_blank>Open '+devUrl+'</a>';out.appendChild(d);log.textContent+='✓ '+j.hostname+' → '+j.ip+'\\n';}}
    catch(e){log.textContent+='· '+url+' — '+(e.message||e)+'\\n';}
  }));
  if(!found.length)log.textContent+='No devices. Check same Wi-Fi and try IP.\\n';
}
async function addIp(){await scan();}
</script>`;
    const blob = new Blob([html], { type: "text/html" });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = "printdrop-scanner.html";
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 2000);
  }, []);

  return (
    <div className="mx-auto max-w-7xl px-4 py-10 sm:px-6">
      <div className="max-w-3xl">
        <Badge variant="secondary">Local discovery</Badge>
        <h1 className="mt-4 text-4xl font-semibold tracking-[-0.04em]">Devices on your network</h1>
        <p className="mt-4 leading-7 text-muted-foreground">
          Probes mDNS <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">printdrop.local</code> and LLMNR{" "}
          <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">printdrop</code> (plus{" "}
          <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">-2</code>…<code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">-6</code>) via{" "}
          <code className="rounded bg-muted px-1 py-0.5 font-mono text-xs">/api/status</code>. Works when the scanner and the stick are on the same
          LAN. If you renamed a stick, change the prefix below.
        </p>

        {isHttps && (
          <Card className="mt-6 border-amber-200 bg-amber-50 dark:border-amber-900 dark:bg-amber-950/30">
            <CardHeader>
              <CardTitle className="flex items-center gap-2 text-amber-900 dark:text-amber-100">
                <AlertTriangle className="size-4" />
                This page is https, the device is http
              </CardTitle>
              <CardDescription className="text-amber-800 dark:text-amber-200">
                <strong>Chrome/Edge:</strong> first <em>Scan</em> will trigger a one-click prompt “Allow this site to access your local network?”
                — click <strong>Allow</strong> and the scan will work (via Private Network Access). The stick now sends{" "}
                <code>Access-Control-Allow-Private-Network: true</code>. <br />
                <strong>Firefox/Safari</strong> don’t support that yet — download the standalone scanner below and open it via{" "}
                <code>file://</code> (double-click). Direct <code>http://printdrop.local</code> links below always work.
              </CardDescription>
            </CardHeader>
          </Card>
        )}

        <Card className="mt-6">
          <CardHeader>
            <CardTitle className="flex items-center gap-2">
              <Search className="size-4" /> Scan
            </CardTitle>
            <CardDescription>
              Hostnames are sanitized to <code>a-z 0-9 -</code> (1–63 chars). If <code>printdrop</code> is taken, the device now auto-uses{" "}
              <code>printdrop-2</code> and the API will suggest the free name on save.
            </CardDescription>
          </CardHeader>
          <CardContent className="grid gap-4">
            <div className="flex flex-wrap items-end gap-3">
              <label className="grid gap-1 text-sm">
                <span className="text-muted-foreground">Hostname prefix</span>
                <input
                  value={base}
                  onChange={(e) => setBase(e.target.value)}
                  placeholder="printdrop"
                  className="rounded-md border bg-background px-3 py-2 font-mono text-sm outline-none focus-visible:ring-2 focus-visible:ring-ring"
                />
              </label>
              <label className="grid gap-1 text-sm">
                <span className="text-muted-foreground">Or try IP</span>
                <input
                  value={customIp}
                  onChange={(e) => setCustomIp(e.target.value)}
                  placeholder="10.74.179.223"
                  className="rounded-md border bg-background px-3 py-2 font-mono text-sm outline-none focus-visible:ring-2 focus-visible:ring-ring"
                />
              </label>
              <Button onClick={scan} disabled={scanning}>
                {scanning ? "Scanning…" : "Scan now"}
              </Button>
              <Button variant="outline" onClick={downloadStandalone}>
                Download standalone scanner
              </Button>
            </div>

            <div className="flex flex-wrap gap-2 text-xs">
              <span className="inline-flex items-center gap-1 rounded-md border px-2 py-1">
                <Globe className="size-3" /> try <code>http://{base.trim() || "printdrop"}.local</code>
              </span>
              <span className="inline-flex items-center gap-1 rounded-md border px-2 py-1">
                <Radio className="size-3" /> + LLMNR <code>http://{base.trim() || "printdrop"}</code>
              </span>
              <a href={`http://${(base.trim() || "printdrop")}.local`} target="_blank" rel="noopener" className="inline-flex items-center gap-1 rounded-md border px-2 py-1 hover:bg-accent">
                Open {base.trim() || "printdrop"}.local <ExternalLink className="size-3" />
              </a>
            </div>
          </CardContent>
        </Card>
      </div>

      {found.length > 0 && (
        <div className="mt-8 grid gap-4 md:grid-cols-2 lg:grid-cols-3">
          {found.map((f) => (
            <Card key={f.status.ip} className="overflow-hidden">
              <CardHeader>
                <CardTitle className="flex items-center justify-between">
                  <span className="flex items-center gap-2">
                    <HardDrive className="size-4" />
                    {f.status.hostname}
                  </span>
                  <Badge variant={f.status.card.present ? "secondary" : "destructive"}>{f.status.card.present ? "card ok" : "no card"}</Badge>
                </CardTitle>
                <CardDescription className="font-mono text-xs">
                  {f.status.ip} · {f.status.ssid || "—"} · {f.status.rssi} dBm · up {fmtUptime(f.status.uptimeMs)}
                </CardDescription>
              </CardHeader>
              <CardContent className="grid gap-3 text-sm">
                <div className="grid grid-cols-2 gap-2 rounded-lg border bg-card p-3 font-mono text-xs">
                  <span className="text-muted-foreground">Version</span>
                  <span className="text-right">{f.status.version}</span>
                  <span className="text-muted-foreground">Bus</span>
                  <span className="text-right">
                    {f.status.card.busMode} {f.status.card.busWidth}-bit @ {Math.round(f.status.card.busHz / 1e6)} MHz
                  </span>
                  <span className="text-muted-foreground">Storage</span>
                  <span className="text-right">
                    {fmtBytes(f.status.card.freeBytes)} free / {fmtBytes(f.status.card.totalBytes)}
                  </span>
                  <span className="text-muted-foreground">USB</span>
                  <span className="text-right">{f.status.usb.mediaPresent ? "presented" : "withdrawn"}</span>
                  <span className="text-muted-foreground">Discovery</span>
                  <span className="text-right">
                    mDNS {f.status.discovery.mdns ? "on" : "off"} · LLMNR {f.status.discovery.llmnr ? "on" : "off"}
                  </span>
                </div>
                <div className="flex gap-2">
                  <Button asChild className="flex-1">
                    <a href={f.url} target="_blank" rel="noopener">
                      Open dashboard <ExternalLink className="size-3" />
                    </a>
                  </Button>
                  <Button variant="outline" asChild>
                    <a href={`${f.url}/api/status`} target="_blank" rel="noopener">
                      <Activity className="size-3" /> status
                    </a>
                  </Button>
                </div>
                <div className="flex items-center gap-2 text-xs text-muted-foreground">
                  <Wifi className="size-3" /> {f.status.mode} · <Cpu className="size-3" /> {f.status.name} · auth {f.status.auth.required ? f.status.auth.user : "off"}
                </div>
              </CardContent>
            </Card>
          ))}
        </div>
      )}

      {found.length === 0 && !scanning && (
        <Card className="mt-6 border-dashed">
          <CardContent className="p-6 text-sm text-muted-foreground">
            No devices yet. Make sure the PrintDrop and this browser are on the same Wi-Fi, then hit <em>Scan now</em>. Tip: set each stick to a
            unique hostname via serial <code>hostname printdrop-2</code> or Web UI <code>Settings → Hostname</code> — if you try a taken name the API
            now replies <code>409 Hostname already taken, try printdrop-2</code> and the device auto-falls back to the free <code>-2</code> on boot.
          </CardContent>
        </Card>
      )}

      <Card className="mt-6 bg-muted/30">
        <CardContent className="p-4 font-mono text-xs leading-6">
          <div className="font-semibold">Log</div>
          <pre className="mt-2 max-h-48 overflow-auto whitespace-pre-wrap break-words text-muted-foreground">
            {log.join("\n") || "— idle —"}
          </pre>
        </CardContent>
      </Card>

      <div className="mt-6 text-sm text-muted-foreground">
        <Link href="/" className="underline-offset-4 hover:underline">
          ← Back to home
        </Link>
      </div>
    </div>
  );
}
