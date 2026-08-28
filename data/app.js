/* PrintDrop — app.js — vanilla JS, no build step */
/* eslint-disable */
(() => {
  'use strict';

  // --- Mock fallback when opened without device (file:// or no /api/status) ---
  const MOCK_STATUS = {name:"PrintDrop",version:"0.1.0",hostname:"printdrop",ip:"192.168.1.50",ssid:"OfficeWifi",rssi:-52,mode:"sta",card:{present:true,totalBytes:31896633344,usedBytes:8421376000,freeBytes:23475257344},usb:{hostPresent:true,mediaPresent:true},uptimeMs:12345678};
  const MOCK_FS = {
    "/":[
      {name:"bracket.gcode",dir:false,size:8421376},
      {name:"calibration-cube.gcode",dir:false,size:1245184},
      {name:"benchy.stl",dir:false,size:3124444},
      {name:"archive",dir:true,size:0},
      {name:"readme.txt",dir:false,size:812},
      {name:"backup.zip",dir:false,size:5237760}
    ],
    "/archive":[
      {name:"old-benchy.gcode",dir:false,size:1048576},
      {name:"photos",dir:true,size:0}
    ],
    "/archive/photos":[
      {name:"preview.3mf",dir:false,size:204800}
    ]
  };
  let useMock = false;

  // --- State ---
  let status = null;
  let curPath = '/';
  let entries = [];
  let viewMode = localStorage.getItem('printdrop.view') || 'grid';
  let sortKey = 'name-asc';
  let pollTimer = null;
  let wsSocket = null;
  let authUser = localStorage.getItem('printdrop.authUser') || localStorage.getItem('printdrop.authUser') || '';
  let authPass = localStorage.getItem('printdrop.authPass') || localStorage.getItem('printdrop.authPass') || '';
  // compat: also check printdrop.authUser/Pass keys used below
  authUser = localStorage.getItem('printdrop.authUser') || authUser;
  authPass = localStorage.getItem('printdrop.authPass') || authPass;

  const $ = (s, r=document) => r.querySelector(s);
  const $$ = (s, r=document) => [...r.querySelectorAll(s)];

  function authHeader(){
    if(authUser && authPass) return 'Basic ' + btoa(authUser + ':' + authPass);
    return null;
  }
  function setAuth(u,p){
    authUser = u; authPass = p;
    localStorage.setItem('printdrop.authUser', u);
    localStorage.setItem('printdrop.authPass', p);
  }
  function clearAuth(){ localStorage.removeItem('printdrop.authUser'); localStorage.removeItem('printdrop.authPass'); authUser=''; authPass=''; }
  async function promptAuth(){
    const u = await openModal({title:'Login required',msg:'Enter PrintDrop login from platformio.ini or serial `auth`',showInput:true,inputValue:authUser,placeholder:'username',okText:'Login'});
    if(u===false) return false;
    const p = await openModal({title:'Password',msg:'Password for '+u,showInput:true,placeholder:'password',okText:'Login'});
    if(p===false) return false;
    setAuth(String(u).trim(), String(p));
    return true;
  }
  function connectWS(){
    try{
      const proto = location.protocol === 'https:' ? 'wss://' : 'ws://';
      const url = proto + location.hostname + ':81/';
      wsSocket = new WebSocket(url);
      wsSocket.onmessage = (ev)=>{
        try{
          const msg = JSON.parse(ev.data);
          if(msg.type==='progress'){
            // update matching qitem if visible
            const el = document.querySelector(`.qitem__name`);
            // generic toast for remote progress
            // console.log('ws progress',msg);
          } else if(msg.type==='status'){
            renderStatus(msg.data);
          }
        }catch{}
      };
      wsSocket.onopen = ()=> console.log('[ws] connected');
      wsSocket.onclose = ()=> setTimeout(connectWS, 5000);
      wsSocket.onerror = ()=> {};
    }catch{}
  }

  // --- Helpers ---
  function fmtBytes(n){
    if(n==null||isNaN(n)) return '—';
    if(n===0) return '0 B';
    const u=['B','KB','MB','GB','TB']; let i=0; let v=n;
    while(v>=1024&&i<u.length-1){v/=1024;i++;}
    return (v>=10?Math.round(v):Math.round(v*10)/10)+' '+u[i];
  }
  function fmtRate(bps){
    if(!isFinite(bps)||bps<=0) return '—';
    return fmtBytes(bps)+'/s';
  }
  function fmtEta(s){
    if(!isFinite(s)||s<0) return '—';
    if(s<1) return 'a sec';
    if(s<60) return Math.round(s)+'s';
    const m=Math.floor(s/60), sec=Math.round(s%60);
    if(m<60) return m+'m '+(sec?sec+'s':'');
    const h=Math.floor(m/60); return h+'h '+ (m%60)+'m';
  }
  function fmtUptime(ms){
    const s=Math.floor(ms/1000);
    const d=Math.floor(s/86400), h=Math.floor(s%86400/3600), m=Math.floor(s%3600/60);
    if(d) return d+'d '+h+'h';
    if(h) return h+'h '+m+'m';
    if(m) return m+'m '+ (s%60)+'s';
    return s+'s';
  }
  function rssiLabel(r){
    if(r==null) return '—';
    if(r>=-50) return 'Excellent ('+r+' dBm)';
    if(r>=-60) return 'Good ('+r+' dBm)';
    if(r>=-70) return 'Fair ('+r+' dBm)';
    return 'Weak ('+r+' dBm)';
  }
  function barsForRssi(r){
    let n=1; if(r>=-50) n=4; else if(r>=-60) n=3; else if(r>=-67) n=2;
    return n;
  }
  function normPath(p){
    if(!p.startsWith('/')) p='/'+p;
    p=p.replace(/\/+/g,'/'); if(p.length>1&&p.endsWith('/')) p=p.slice(0,-1);
    return p||'/';
  }
  function joinPath(base,name){ base=normPath(base); return normPath(base==='/'?'/'+name:base+'/'+name); }
  function parentPath(p){ p=normPath(p); if(p==='/') return '/'; const i=p.lastIndexOf('/'); return i<=0?'/':p.slice(0,i); }
  function extOf(name){ const i=name.lastIndexOf('.'); return i>=0?name.slice(i+1).toLowerCase():''; }
  function kindFor(entry){
    if(entry.dir) return 'folder';
    const e=extOf(entry.name);
    if(['gcode','gco','g','bgcode'].includes(e)) return 'gcode';
    if(['stl','3mf','obj','step','stp'].includes(e)) return 'model';
    if(['zip','rar','7z'].includes(e)) return 'archive';
    if(['txt','log','ini','cfg','json'].includes(e)) return 'doc';
    return 'file';
  }
  function typeLabel(k){
    return {gcode:'G-code',model:'3D model',folder:'Folder',archive:'Archive',doc:'Document',file:'File'}[k]||'File';
  }
  // inline SVG per kind (small, no external)
  function iconSVG(kind){
    const base='width="28" height="28" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"';
    if(kind==='folder') return `<svg ${base}><path d="M3 7a2 2 0 0 1 2-2h4l2 2h6a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V7z"/></svg>`;
    if(kind==='gcode') return `<svg ${base}><rect x="3" y="7" width="18" height="10" rx="2"/><path d="M8 7V5a4 4 0 0 1 8 0v2"/><path d="M12 11v4"/><path d="M9 14h6"/><circle cx="12" cy="8.5" r="1" fill="currentColor" stroke="none"/></svg>`;
    if(kind==='model') return `<svg ${base}><path d="M12 2L3 7v10l9 5 9-5V7z"/><path d="M3 7l9 5 9-5"/><path d="M12 12v10"/></svg>`;
    if(kind==='archive') return `<svg ${base}><rect x="3" y="7" width="18" height="12" rx="2"/><path d="M3 10h18"/><path d="M12 14v4"/><path d="M9 15l3 3 3-3"/></svg>`;
    if(kind==='doc') return `<svg ${base}><path d="M14 2H7a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V8z"/><polyline points="14 2 14 8 20 8"/><line x1="8" y1="13" x2="16" y2="13"/><line x1="8" y1="17" x2="13" y2="17"/></svg>`;
    return `<svg ${base}><path d="M14 2H7a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V8z"/><polyline points="14 2 14 8 20 8"/></svg>`;
  }

  function toast(msg, kind='ok'){
    const w=$('#toastWrap'); const t=document.createElement('div');
    t.className='toast toast--'+(kind==='error'?'err':kind==='warn'?'warn':'ok');
    t.textContent=msg; w.appendChild(t);
    setTimeout(()=>{ t.style.opacity='0'; t.style.transform='translateY(4px)'; t.style.transition='all .2s'; }, 2600);
    setTimeout(()=>t.remove(), 3000);
  }

  // --- Theme ---
  function applyTheme(v){
    if(v==='dark'||v==='light') document.documentElement.setAttribute('data-theme', v);
    else document.documentElement.removeAttribute('data-theme');
  }
  function initTheme(){
    const s=localStorage.getItem('printdrop.theme');
    if(s) applyTheme(s);
  }
  $('#themeToggle').addEventListener('click',()=>{
    const cur=document.documentElement.getAttribute('data-theme');
    let next;
    if(!cur){
      next = matchMedia('(prefers-color-scheme: dark)').matches ? 'light':'dark';
    } else next = cur==='dark'?'light':'dark';
    localStorage.setItem('printdrop.theme', next);
    applyTheme(next);
  });

  // --- Navigation (instant, no reload) ---
  function showView(id){
    $$('.view').forEach(v=>v.classList.remove('is-active'));
    $$('.nav__btn').forEach(b=>b.classList.remove('is-active'));
    const sec=$('#view-'+id); if(sec) sec.classList.add('is-active');
    const btn=$(`.nav__btn[data-view="${id}"]`); if(btn){btn.classList.add('is-active'); btn.setAttribute('aria-current','page');}
    $$('.nav__btn').forEach(b=>{ if(b!==btn) b.removeAttribute('aria-current'); });
    // close mobile drawer
    $('#sidebar').classList.remove('is-open');
    $('#mobileNavToggle').setAttribute('aria-expanded','false');
  }
  $$('.nav__btn').forEach(b=> b.addEventListener('click',()=>showView(b.dataset.view)));
  $$('[data-view-jump]').forEach(b=> b.addEventListener('click',()=>showView(b.dataset.viewJump)));
  $('#mobileNavToggle').addEventListener('click',()=>{
    const sb=$('#sidebar'); const open=sb.classList.toggle('is-open');
    $('#mobileNavToggle').setAttribute('aria-expanded', String(open));
  });

  // --- View mode toggle ---
  function applyViewMode(m){
    viewMode=m; localStorage.setItem('printdrop.view', m);
    $('#gridBtn').classList.toggle('is-active', m==='grid');
    $('#listBtn').classList.toggle('is-active', m==='list');
    $('#gridBtn').setAttribute('aria-pressed', String(m==='grid'));
    $('#listBtn').setAttribute('aria-pressed', String(m==='list'));
    renderFiles();
  }
  $('#gridBtn').addEventListener('click',()=>applyViewMode('grid'));
  $('#listBtn').addEventListener('click',()=>applyViewMode('list'));
  $('#sortSelect').addEventListener('change', e=>{ sortKey=e.target.value; renderFiles(); });

  // --- API wrappers (mock aware) ---
  function withAuthHeaders(h={}){
    const ah = authHeader();
    if(ah) h['Authorization']=ah;
    return h;
  }
  async function handleAuthFail(r){
    if(r.status===401){
      const ok = await promptAuth();
      if(ok) return true;
    }
    return false;
  }
  async function apiStatus(){
    if(useMock){ await new Promise(r=>setTimeout(r,120)); return structuredClone(MOCK_STATUS); }
    const r=await fetch('/api/status',{cache:'no-store',headers:withAuthHeaders()});
    if(r.status===401 && await handleAuthFail(r)){ return apiStatus(); }
    if(!r.ok) throw new Error('status '+r.status);
    return r.json();
  }
  async function apiList(path){
    if(useMock){ await new Promise(r=>setTimeout(r,100)); return {path, entries: structuredClone(MOCK_FS[path]||[])}; }
    const r=await fetch('/api/list?path='+encodeURIComponent(path),{cache:'no-store',headers:withAuthHeaders()});
    if(r.status===401 && await handleAuthFail(r)){ return apiList(path); }
    if(!r.ok) throw new Error('list '+r.status);
    return r.json();
  }
  async function apiPostForm(url, data){
    if(useMock){
      await new Promise(r=>setTimeout(r,250));
      // mutate mock FS
      if(url==='/api/delete'){ const p=data.path; const dir=parentPath(p), name=p.slice(p.lastIndexOf('/')+1); MOCK_FS[dir]=(MOCK_FS[dir]||[]).filter(e=>e.name!==name); return {ok:true}; }
      if(url==='/api/mkdir'){ const p=normPath(data.path); const dir=parentPath(p), name=p.slice(p.lastIndexOf('/')+1); if(!MOCK_FS[dir]) MOCK_FS[dir]=[]; if(MOCK_FS[dir].some(e=>e.name===name)) return {ok:false,error:'Already exists'}; MOCK_FS[dir].push({name,dir:true,size:0}); MOCK_FS[p]=[]; return {ok:true}; }
      if(url==='/api/rename'){ const from=normPath(data.from), to=normPath(data.to); const fd=parentPath(from), td=parentPath(to); const fn=from.slice(from.lastIndexOf('/')+1), tn=to.slice(to.lastIndexOf('/')+1); const ent=(MOCK_FS[fd]||[]).find(e=>e.name===fn); if(!ent) return {ok:false,error:'Not found'}; if((MOCK_FS[td]||[]).some(e=>e.name===tn)) return {ok:false,error:'Destination exists'}; MOCK_FS[fd]=MOCK_FS[fd].filter(e=>e.name!==fn); if(!MOCK_FS[td]) MOCK_FS[td]=[]; MOCK_FS[td].push({name:tn,dir:ent.dir,size:ent.size}); if(ent.dir){ MOCK_FS[to]=MOCK_FS[from]||[]; delete MOCK_FS[from]; } return {ok:true}; }
      if(url==='/api/eject') return {ok:true};
      if(url==='/api/wifi') return {ok:true};
      if(url==='/api/auth/set') return {ok:true};
      if(url==='/api/ota/sd') return {ok:true};
      return {ok:true};
    }
    const r=await fetch(url,{method:'POST',headers:{...withAuthHeaders(),'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data).toString()});
    if(r.status===401 && await handleAuthFail(r)){ return apiPostForm(url,data); }
    const j=await r.json().catch(()=>({ok:false,error:'Bad response'}));
    return j;
  }
  async function apiWifiScan(){
    if(useMock){ await new Promise(r=>setTimeout(r,500)); return {networks:[{ssid:'OfficeWifi',rssi:-46,secure:true},{ssid:'Guest',rssi:-62,secure:false},{ssid:'Lab-AP',rssi:-71,secure:true}]}; }
    const r=await fetch('/api/wifi/scan',{cache:'no-store',headers:withAuthHeaders()}); if(r.status===401 && await handleAuthFail(r)){ return apiWifiScan(); } if(!r.ok) throw new Error('scan '+r.status); return r.json();
  }

  // --- Status UI ---
  function renderStatus(s){
    status=s;
    $('#versionBadge').textContent=s.version||'0.1.0';
    $('#aboutVersion').textContent=s.version||'0.1.0';
    const pct = s.card && s.card.totalBytes ? Math.min(100, Math.round(s.card.usedBytes / s.card.totalBytes * 100)) : 0;
    $('#storageBar').style.width=pct+'%';
    $('#storageMiniBar').style.width=pct+'%';
    $('#storageUsed').textContent=fmtBytes(s.card.usedBytes)+' used';
    $('#storageFree').textContent=fmtBytes(s.card.freeBytes)+' free';
    $('#storageTotal').textContent='of '+fmtBytes(s.card.totalBytes);
    $('#storageMiniText').textContent= s.card.present ? pct+'% · '+fmtBytes(s.card.freeBytes)+' free' : 'No card';
    $('#cardPresentMsg').textContent = s.card.present ? '' : 'No SD card detected — insert a FAT32 card.';
    $('#kvHostname').textContent=s.hostname||'—';
    $('#kvIp').textContent=s.ip||'—';
    $('#kvSsid').textContent=s.ssid||'—';
    $('#kvRssi').textContent=rssiLabel(s.rssi);
    $('#kvMode').textContent=s.mode||'—';
    $('#kvUptime').textContent=fmtUptime(s.uptimeMs||0);
    const usbMsg=$('#usbMsg');
    if(!s.usb) usbMsg.textContent='Unknown';
    else if(!s.usb.hostPresent) usbMsg.textContent='No printer connected — card not visible to a host.';
    else if(s.usb.mediaPresent) usbMsg.textContent='Printer connected — card visible to printer.';
    else usbMsg.textContent='Printer connected — card not presented (eject or check SD).';
    // conn dot
    const dot=$('#connDot'), txt=$('#connText');
    dot.classList.remove('is-ok','is-warn');
    if(useMock){ dot.classList.add('is-warn'); txt.textContent='Preview (no device)'; }
    else { dot.classList.add('is-ok'); txt.textContent=s.ssid? s.ssid+' · '+s.ip : (s.ip||'Connected'); }
    // discovery
    const disc=$('#discInfo'); if(disc && s.hostname) disc.textContent = s.hostname+'.local / '+s.hostname+'  (mDNS'+(s.discovery&&s.discovery.llmnr?' + LLMNR':'')+')';
    // ota
    const oc=$('#otaCurrent'); if(oc) oc.textContent = s.ota ? s.ota.version : (s.version||'—');
    const enBadge=$('#otaEnabledBadge'); if(enBadge) enBadge.textContent = s.ota && s.ota.enabled ? 'OTA on' : 'OTA off';
    const sdBox=$('#otaSDBox'); if(sdBox){
      const avail = s.ota && s.ota.sdAvailable;
      sdBox.classList.toggle('hidden', !avail);
      if(avail){ $('#otaSDVer').textContent = s.ota.sdVersion||''; const n=$('#otaSDNotes'); if(n) n.textContent = ''; }
    }
    // auth indicator
    if(s.auth && s.auth.required){
      document.documentElement.setAttribute('data-auth','on');
    }
  }

  async function refreshStatus(){
    try{
      const s=await apiStatus();
      renderStatus(s);
    }catch(e){
      if(!useMock && e.message.includes('Failed to fetch')){
        // handled in init
      }
      $('#connText').textContent='Offline';
    }
  }

  // --- Files ---
  async function loadPath(p){
    p=normPath(p);
    curPath=p; $('#uploadPathLabel').textContent=p; $('#dropPathLabel').textContent='to '+p;
    try{
      const j=await apiList(p);
      entries=j.entries||[];
      renderBreadcrumb(p);
      renderFiles();
    }catch(e){
      toast('Failed to list '+p+': '+e.message,'error');
    }
  }
  function renderBreadcrumb(p){
    const ol=$('#breadcrumb'); ol.innerHTML='';
    const parts=p.split('/').filter(Boolean);
    const crumbs=[{name:'Home',path:'/'}];
    let acc='';
    parts.forEach(seg=>{ acc+='/'+seg; crumbs.push({name:seg,path:acc}); });
    crumbs.forEach((c,i)=>{
      const li=document.createElement('li');
      if(i===crumbs.length-1){
        li.innerHTML=`<span aria-current="page">${esc(c.name)}</span>`;
      } else {
        const btn=document.createElement('button'); btn.type='button'; btn.textContent=c.name; btn.addEventListener('click',()=>loadPath(c.path));
        li.appendChild(btn);
      }
      ol.appendChild(li);
    });
  }
  function esc(s){ return s.replace(/[&<>"']/g, c=>({ '&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c])); }

  function sortedEntries(){
    const a=[...entries];
    const [key,dir]=sortKey.split('-');
    a.sort((x,y)=>{
      if(x.dir!==y.dir) return x.dir? -1:1; // folders first
      if(key==='size') return dir==='asc'? x.size-y.size : y.size-x.size;
      const c=x.name.toLowerCase().localeCompare(y.name.toLowerCase());
      return dir==='asc'? c : -c;
    });
    return a;
  }

  function renderFiles(){
    const sorted=sortedEntries();
    const emptyEl=$('#filesEmpty'), grid=$('#fileGrid'), wrap=$('#fileListWrap');
    if(sorted.length===0){
      emptyEl.classList.remove('hidden'); grid.innerHTML=''; $('#fileListBody').innerHTML=''; grid.classList.add('hidden'); wrap.classList.add('hidden'); return;
    }
    emptyEl.classList.add('hidden');
    if(viewMode==='grid'){
      grid.classList.remove('hidden'); wrap.classList.add('hidden');
      grid.innerHTML='';
      sorted.forEach(ent=>{
        const kind=kindFor(ent);
        const card=document.createElement('div');
        card.className='file-card file-card--'+kind + (ent.dir?' file-card--folder':'');
        card.tabIndex=0; card.setAttribute('role','button'); card.setAttribute('aria-label',(ent.dir?'Folder ':'')+ent.name);
        card.innerHTML=`<div class="file-card__icon">${iconSVG(kind)}</div>
          <div class="file-card__name" title="${esc(ent.name)}">${esc(ent.name)}</div>
          <div class="file-card__meta"><span>${ent.dir?'Folder':fmtBytes(ent.size)}</span><span>${typeLabel(kind)}</span></div>
          <div class="file-card__actions"></div>`;
        const actions=card.querySelector('.file-card__actions');
        if(ent.dir){
          const openBtn=document.createElement('button'); openBtn.className='btn btn--secondary'; openBtn.type='button'; openBtn.textContent='Open';
          openBtn.addEventListener('click',e=>{ e.stopPropagation(); loadPath(joinPath(curPath, ent.name)); });
          actions.appendChild(openBtn);
        } else {
          const dl=document.createElement('button'); dl.className='btn btn--secondary'; dl.type='button'; dl.setAttribute('aria-label','Download '+ent.name); dl.innerHTML='Download';
          dl.addEventListener('click',e=>{ e.stopPropagation(); doDownload(ent); });
          actions.appendChild(dl);
        }
        const rn=document.createElement('button'); rn.className='btn btn--ghost'; rn.type='button'; rn.textContent='Rename'; rn.setAttribute('aria-label','Rename '+ent.name);
        rn.addEventListener('click',e=>{ e.stopPropagation(); doRename(ent); });
        const del=document.createElement('button'); del.className='btn btn--ghost'; del.type='button'; del.textContent='Delete'; del.setAttribute('aria-label','Delete '+ent.name);
        del.addEventListener('click',e=>{ e.stopPropagation(); doDelete(ent); });
        actions.appendChild(rn); actions.appendChild(del);
        card.addEventListener('click',()=>{ if(ent.dir) loadPath(joinPath(curPath, ent.name)); });
        card.addEventListener('keydown',e=>{ if(e.key==='Enter'||e.key===' '){ e.preventDefault(); if(ent.dir) loadPath(joinPath(curPath, ent.name)); }});
        grid.appendChild(card);
      });
    } else {
      grid.classList.add('hidden'); wrap.classList.remove('hidden');
      const tbody=$('#fileListBody'); tbody.innerHTML='';
      sorted.forEach(ent=>{
        const kind=kindFor(ent);
        const tr=document.createElement('tr');
        const nameTd=document.createElement('td');
        nameTd.innerHTML=`<span style="display:inline-flex;align-items:center;gap:8px"><span style="width:22px;height:22px;display:grid;place-items:center;background:var(--surface-2);border:1px solid var(--border);border-radius:6px">${iconSVG(kind).replace('width="28"','width="16"').replace('height="28"','height="16"')}</span> ${esc(ent.name)}</span>`;
        if(ent.dir){ nameTd.style.cursor='pointer'; nameTd.addEventListener('click',()=>loadPath(joinPath(curPath, ent.name))); nameTd.title='Open folder'; }
        const sizeTd=document.createElement('td'); sizeTd.textContent= ent.dir?'—':fmtBytes(ent.size);
        const typeTd=document.createElement('td'); typeTd.textContent=typeLabel(kind);
        const actTd=document.createElement('td'); actTd.className='row-actions';
        if(!ent.dir){
          const dl=document.createElement('button'); dl.className='btn btn--secondary'; dl.type='button'; dl.textContent='Download'; dl.setAttribute('aria-label','Download '+ent.name);
          dl.addEventListener('click',()=>doDownload(ent)); actTd.appendChild(dl);
        } else {
          const o=document.createElement('button'); o.className='btn btn--secondary'; o.type='button'; o.textContent='Open';
          o.addEventListener('click',()=>loadPath(joinPath(curPath, ent.name))); actTd.appendChild(o);
        }
        const rn=document.createElement('button'); rn.className='btn btn--ghost'; rn.type='button'; rn.textContent='Rename'; rn.addEventListener('click',()=>doRename(ent)); actTd.appendChild(rn);
        const del=document.createElement('button'); del.className='btn btn--ghost'; del.type='button'; del.textContent='Delete'; del.setAttribute('aria-label','Delete '+ent.name);
        del.addEventListener('click',()=>doDelete(ent)); actTd.appendChild(del);
        tr.append(nameTd,sizeTd,typeTd,actTd); tbody.appendChild(tr);
      });
    }
  }

  // table header sort
  $$('.table th[data-sort]').forEach(th=>{
    th.addEventListener('click',()=>{
      const k=th.dataset.sort;
      if(k==='name') sortKey = sortKey==='name-asc'?'name-desc':'name-asc';
      else sortKey = sortKey==='size-desc'?'size-asc':'size-desc';
      $('#sortSelect').value=sortKey;
      renderFiles();
    });
  });

  // --- File actions ---
  async function doDownload(ent){
    const url='/api/download?path='+encodeURIComponent(joinPath(curPath, ent.name));
    if(useMock){ toast('Mock: download '+ent.name,'warn'); return; }
    const ah=authHeader();
    if(ah){
      try{
        const r=await fetch(url,{headers:{'Authorization':ah}});
        if(r.status===401){ if(await promptAuth()) return doDownload(ent); else return; }
        if(!r.ok) throw new Error('download '+r.status);
        const blob=await r.blob();
        const a=document.createElement('a'); a.href=URL.createObjectURL(blob); a.download=ent.name; document.body.appendChild(a); a.click(); setTimeout(()=>{ URL.revokeObjectURL(a.href); a.remove(); }, 1000);
        return;
      }catch(e){ toast('Download failed: '+e.message,'error'); return; }
    }
    const a=document.createElement('a'); a.href=url; a.download=ent.name; document.body.appendChild(a); a.click(); a.remove();
  }

  // modal helper returns Promise<string|false>
  let modalResolve=null;
  function openModal({title,msg,placeholder,inputValue,okText,cancelText,showInput}){
    $('#modalTitle').textContent=title||'';
    $('#modalMsg').textContent=msg||'';
    const wrap=$('#modalInputWrap'), inp=$('#modalInput');
    if(showInput){ wrap.classList.remove('hidden'); inp.value=inputValue||''; inp.placeholder=placeholder||''; setTimeout(()=>inp.focus(),30); } else wrap.classList.add('hidden');
    $('#modalOk').textContent=okText||'Confirm';
    $('#modalCancel').textContent=cancelText||'Cancel';
    $('#modal').classList.remove('hidden');
    return new Promise(res=>{ modalResolve=res; });
  }
  function closeModal(v){
    $('#modal').classList.add('hidden');
    if(modalResolve){ const r=modalResolve; modalResolve=null; r(v); }
  }
  $('#modalCancel').addEventListener('click',()=>closeModal(false));
  $('[data-close-modal]').addEventListener('click',()=>closeModal(false));
  $('#modalOk').addEventListener('click',()=>{
    const wrap=$('#modalInputWrap');
    if(!wrap.classList.contains('hidden')) closeModal($('#modalInput').value);
    else closeModal(true);
  });
  $('#modalInput').addEventListener('keydown',e=>{
    if(e.key==='Enter'){ e.preventDefault(); closeModal(e.target.value); }
    if(e.key==='Escape') closeModal(false);
  });
  document.addEventListener('keydown',e=>{ if(e.key==='Escape' && !$('#modal').classList.contains('hidden')) closeModal(false); });

  async function doDelete(ent){
    const ok=await openModal({title:'Delete '+(ent.dir?'folder':'file')+'?',msg: ent.name+' — this cannot be undone.',okText:'Delete',cancelText:'Cancel'});
    if(!ok) return;
    const path=joinPath(curPath, ent.name);
    const r=await apiPostForm('/api/delete',{path});
    if(r.ok){ toast('Deleted '+ent.name); loadPath(curPath); refreshStatus(); } else toast(r.error||'Delete failed','error');
  }
  async function doRename(ent){
    const from=joinPath(curPath, ent.name);
    const val=await openModal({title:'Rename',msg:'Enter a new name for '+ent.name,showInput:true,inputValue:ent.name,placeholder:'New name',okText:'Rename'});
    if(val===false) return;
    const toName=String(val).trim();
    if(!toName || toName===ent.name) return;
    if(toName.includes('/')){ toast('Name cannot contain /','error'); return; }
    const to=joinPath(curPath, toName);
    const r=await apiPostForm('/api/rename',{from,to});
    if(r.ok){ toast('Renamed to '+toName); loadPath(curPath); } else toast(r.error||'Rename failed','error');
  }
  $('#newFolderBtn').addEventListener('click', async()=>{
    const name=await openModal({title:'New folder',msg:'Create a folder in '+curPath,showInput:true,placeholder:'Folder name',okText:'Create'});
    if(name===false) return;
    const n=String(name).trim(); if(!n){ toast('Enter a name','warn'); return; }
    if(n.includes('/')){ toast('Name cannot contain /','error'); return; }
    const r=await apiPostForm('/api/mkdir',{path:joinPath(curPath, n)});
    if(r.ok){ toast('Folder created'); loadPath(curPath); } else toast(r.error||'Create failed','error');
  });

  // --- Upload (XHR for progress) ---
  const queue=[]; let uploading=false;
  function handleFiles(fileList){
    if(!fileList||!fileList.length) return;
    showView('upload');
    const existing=new Set(entries.map(e=>e.name));
    [...fileList].forEach(f=>{
      const dup=existing.has(f.name);
      addQueueItem(f,dup);
    });
    pump();
  }
  function addQueueItem(file, duplicate){
    const id=Math.random().toString(36).slice(2);
    const el=document.createElement('div'); el.className='qitem'; el.dataset.id=id;
    el.innerHTML=`<div class="qitem__head"><span class="qitem__name">${esc(file.name)} <span class="muted muted--sm">(${fmtBytes(file.size)})</span></span><span class="badge">${duplicate?'<span class="badge badge--warn">overwrite</span>':''}</span></div>
      ${duplicate?'<div class="badge badge--warn" style="margin-top:6px;display:inline-block">A file with this name already exists — uploading will overwrite it.</div>':''}
      <div class="qitem__meta"><span class="q-pct">Queued</span><span class="q-rate"></span><span class="q-eta"></span></div>
      <div class="progress qitem__bar"><div class="progress__fill" style="width:0"></div></div>`;
    // fix badge presence
    if(duplicate){ el.querySelector('.badge').innerHTML='overwrite'; el.querySelector('.badge').className='badge badge--warn'; } else el.querySelector('.badge').textContent='queued';
    $('#uploadQueue').prepend(el);
    queue.push({id,file,el,duplicate});
  }
  function pump(){
    if(uploading) return;
    const next=queue.find(q=>!q.done&&!q.started);
    if(!next) return;
    uploading=true; uploadOne(next).finally(()=>{ uploading=false; pump(); });
  }
  function uploadOne(item){
    item.started=true;
    const bar=item.el.querySelector('.progress__fill');
    const pctEl=item.el.querySelector('.q-pct');
    const rateEl=item.el.querySelector('.q-rate');
    const etaEl=item.el.querySelector('.q-eta');
    const badge=item.el.querySelector('.badge');
    badge.textContent='uploading'; badge.className='badge';
    const start=Date.now();
    if(useMock){
      let loaded=0; const total=item.file.size|| 1024*1024;
      return new Promise(res=>{
        const iv=setInterval(()=>{
          loaded=Math.min(total, loaded + Math.max(65536, total*0.08));
          const pct=Math.round(loaded/total*100);
          bar.style.width=pct+'%'; pctEl.textContent=pct+'%';
          const elapsed=(Date.now()-start)/1000; const rate=loaded/elapsed; rateEl.textContent=fmtRate(rate); etaEl.textContent='ETA '+fmtEta((total-loaded)/rate);
          if(loaded>=total){
            clearInterval(iv);
            // add to mock FS
            const dir=curPath; if(!MOCK_FS[dir]) MOCK_FS[dir]=[];
            const idx=MOCK_FS[dir].findIndex(e=>e.name===item.file.name);
            const ent={name:item.file.name,dir:false,size:item.file.size};
            if(idx>=0) MOCK_FS[dir][idx]=ent; else MOCK_FS[dir].push(ent);
            item.done=true; badge.textContent='done'; badge.className='badge badge--ok'; pctEl.textContent='100% — done'; rateEl.textContent=''; etaEl.textContent='';
            toast('Uploaded '+item.file.name); loadPath(curPath); refreshStatus();
            res();
          }
        }, 120);
      });
    }
    return new Promise((resolve)=>{
      const xhr=new XMLHttpRequest();
      xhr.open('POST','/api/upload?path='+encodeURIComponent(curPath));
      const ah = authHeader(); if(ah) xhr.setRequestHeader('Authorization', ah);
      xhr.upload.onprogress=e=>{
        if(e.lengthComputable){
          const pct=Math.round(e.loaded/e.total*100);
          bar.style.width=pct+'%'; pctEl.textContent=pct+'%';
          const elapsed=(Date.now()-start)/1000;
          const rate=elapsed>0? e.loaded/elapsed:0;
          rateEl.textContent=fmtRate(rate);
          etaEl.textContent='ETA '+fmtEta((e.total-e.loaded)/rate);
        }
      };
      xhr.onload=()=>{
        let j=null; try{ j=JSON.parse(xhr.responseText); }catch{}
        if(xhr.status>=200&&xhr.status<300 && j && j.ok){
          item.done=true; badge.textContent='done'; badge.className='badge badge--ok'; bar.style.width='100%'; pctEl.textContent='100% — done'; rateEl.textContent=''; etaEl.textContent='';
          toast('Uploaded '+item.file.name); loadPath(curPath); refreshStatus();
        } else {
          const msg=j&&j.error? j.error : ('Upload failed ('+xhr.status+')');
          badge.textContent='failed'; badge.className='badge badge--err'; pctEl.textContent='failed'; toast(msg,'error');
          item.done=true;
        }
        resolve();
      };
      xhr.onerror=()=>{ badge.textContent='failed'; badge.className='badge badge--err'; pctEl.textContent='network error'; toast('Upload failed — network error','error'); item.done=true; resolve(); };
      const fd=new FormData(); fd.append('file', item.file, item.file.name);
      xhr.send(fd);
    });
  }

  // file input + drag & drop
  $('#fileInput').addEventListener('change', e=>{ handleFiles(e.target.files); e.target.value=''; });
  const overlay=$('#dropOverlay');
  let dragDepth=0;
  function showOverlay(){ overlay.classList.remove('hidden'); }
  function hideOverlay(){ overlay.classList.add('hidden'); }
  window.addEventListener('dragenter', e=>{ e.preventDefault(); dragDepth++; showOverlay(); });
  window.addEventListener('dragover', e=>{ e.preventDefault(); if(e.dataTransfer) e.dataTransfer.dropEffect='copy'; });
  window.addEventListener('dragleave', e=>{
    // only hide when leaving window
    if(e.target===document.documentElement||e.target===document.body) dragDepth=Math.max(0,dragDepth-1);
    else dragDepth=Math.max(0,dragDepth-1);
    if(dragDepth===0) hideOverlay();
  });
  window.addEventListener('drop', e=>{
    e.preventDefault(); dragDepth=0; hideOverlay();
    const files=e.dataTransfer&&e.dataTransfer.files; if(files&&files.length) handleFiles(files);
  });
  overlay.addEventListener('click', hideOverlay);

  // --- Eject ---
  $('#ejectBtn').addEventListener('click', async()=>{
    $('#ejectBtn').disabled=true;
    const r=await apiPostForm('/api/eject',{});
    $('#ejectBtn').disabled=false;
    if(r.ok) toast('Printer view refreshed — it will re-read the card.');
    else toast(r.error||'Eject failed','error');
  });

  // --- Wi-Fi ---
  $('#useStatic').addEventListener('change', e=>{ $('#staticFields').classList.toggle('hidden', !e.target.checked); });
  $('#scanBtn').addEventListener('click', async()=>{
    $('#scanBtn').disabled=true; $('#scanStatus').textContent='Scanning…'; $('#scanList').innerHTML='';
    try{
      const j=await apiWifiScan();
      const list=j.networks||[];
      if(!list.length) $('#scanList').innerHTML='<p class="muted muted--sm">No networks found.</p>';
      else list.sort((a,b)=>b.rssi-a.rssi).forEach(n=>{
        const row=document.createElement('button'); row.type='button'; row.className='scan-item';
        const bars=barsForRssi(n.rssi);
        let barsHtml='<span class="bars" aria-hidden="true">';
        for(let i=1;i<=4;i++) barsHtml+=`<i class="${i<=bars?'on':''}" style="height:${6+i*3}px"></i>`;
        barsHtml+='</span>';
        row.innerHTML=`<span><span class="scan-item__ssid">${esc(n.ssid)}</span> <span class="muted muted--sm">${n.secure?'🔒':''} ${n.rssi} dBm</span></span>${barsHtml}`;
        row.addEventListener('click',()=>{ $('#wifiSsid').value=n.ssid; $('#wifiPass').focus(); });
        $('#scanList').appendChild(row);
      });
      $('#scanStatus').textContent=list.length? 'Tap a network to fill SSID.':'';
      if(!list.length) toast('No networks found','warn');
    }catch(e){ $('#scanStatus').textContent=''; toast('Scan failed: '+e.message,'error'); }
    finally{ $('#scanBtn').disabled=false; }
  });
  $('#wifiForm').addEventListener('submit', async e=>{
    e.preventDefault();
    const fd=new FormData(e.target);
    const data={
      ssid:fd.get('ssid')||'',
      password:fd.get('password')||'',
      hostname:fd.get('hostname')||'',
      useStatic: $('#useStatic').checked ? '1':'0',
      ip:fd.get('ip')||'',
      gw:fd.get('gw')||'',
      mask:fd.get('mask')||'',
      dns:fd.get('dns')||''
    };
    if(!data.ssid){ toast('SSID is required','error'); return; }
    const ok=await openModal({title:'Save Wi-Fi settings?',msg:'The device will reboot and rejoin "'+data.ssid+'". You may need to reconnect.',okText:'Save & reboot',cancelText:'Cancel'});
    if(!ok) return;
    const btn=e.target.querySelector('button[type="submit"]'); btn.disabled=true; btn.textContent='Saving…';
    const r=await apiPostForm('/api/wifi', data);
    btn.disabled=false; btn.textContent='Save & reboot';
    if(r.ok) toast('Settings saved — device rebooting…');
    else toast(r.error||'Save failed','error');
  });

  // --- Auth ---
  const authForm = $('#authForm');
  if(authForm){
    // prefill user from status
    authForm.addEventListener('submit', async e=>{
      e.preventDefault();
      const user=$('#authUser').value.trim();
      const p1=$('#authPass').value;
      const p2=$('#authPass2').value;
      if(!user || !p1){ toast('User and password required','error'); return; }
      if(p1 !== p2){ toast('Passwords do not match','error'); return; }
      const ok=await openModal({title:'Save login?',msg:'You will need to log in with "'+user+'" after reboot.',okText:'Save'});
      if(!ok) return;
      const r=await apiPostForm('/api/auth/set',{user,pass:p1});
      if(r.ok){ toast('Login saved — device keeps it'); setAuth(user,p1); $('#authPass').value=''; $('#authPass2').value=''; $('#authStatus').textContent='Saved'; }
      else toast(r.error||'Save failed','error');
    });
  }

  // --- OTA ---
  const otaFile = $('#otaFile');
  const otaFileName = $('#otaFileName');
  const otaBar = $('#otaBar');
  const otaProgress = $('#otaProgress');
  if(otaFile){
    otaFile.addEventListener('change', async e=>{
      const f=e.target.files[0]; if(!f) return;
      otaFileName.textContent = f.name + ' ('+fmtBytes(f.size)+')';
      const ok=await openModal({title:'Flash firmware?',msg:f.name+' '+fmtBytes(f.size)+' — device will reboot after.',okText:'Flash'});
      if(!ok){ e.target.value=''; return; }
      const xhr=new XMLHttpRequest();
      xhr.open('POST','/api/ota');
      const ah=authHeader(); if(ah) xhr.setRequestHeader('Authorization', ah);
      xhr.upload.onprogress=ev=>{
        if(ev.lengthComputable){
          const pct=Math.round(ev.loaded/ev.total*100);
          otaProgress.classList.remove('hidden');
          otaBar.style.width=pct+'%';
        }
      };
      xhr.onload=()=>{
        let j=null; try{ j=JSON.parse(xhr.responseText);}catch{}
        if(xhr.status>=200&&xhr.status<300 && j&&j.ok){ toast('OTA done — rebooting'); otaBar.style.width='100%'; }
        else toast(j&&j.error?j.error:'OTA failed ('+xhr.status+')','error');
        e.target.value='';
      };
      xhr.onerror=()=>{ toast('OTA network error','error'); e.target.value=''; };
      const fd=new FormData(); fd.append('file', f, f.name);
      otaProgress.classList.remove('hidden'); otaBar.style.width='0';
      xhr.send(fd);
    });
  }
  const otaSDTrigger=$('#otaSDTrigger');
  if(otaSDTrigger){
    otaSDTrigger.addEventListener('click', async()=>{
      otaSDTrigger.disabled=true;
      const r=await apiPostForm('/api/ota/sd',{});
      otaSDTrigger.disabled=false;
      if(r.ok) toast('SD OTA triggered — rebooting');
      else toast(r.error||'SD OTA failed','error');
    });
  }

  // --- Init ---
  async function init(){
    initTheme();
    // apply stored view
    applyViewMode(viewMode);
    // try real status; if fails -> mock preview mode
    try{
      const s=await apiStatus();
      useMock=false; renderStatus(s);
      // prefill auth user
      if(s.auth && s.auth.user){ const au=$('#authUser'); if(au && !au.value) au.value=s.auth.user; }
      connectWS();
    }catch(err){
      useMock=true;
      console.warn('[PrintDrop] /api/status failed — entering mock preview mode', err);
      renderStatus(MOCK_STATUS);
      toast('Preview mode — no device found, showing mock data','warn');
    }
    await loadPath('/');
    // poll every 5s (hard constraint — never faster) — WS pushes supplement this
    pollTimer=setInterval(refreshStatus, 5000);
  }
  // respect reduced motion already via CSS; no JS needed
  document.addEventListener('visibilitychange',()=>{
    if(document.hidden){ clearInterval(pollTimer); pollTimer=null; }
    else if(!pollTimer){ refreshStatus(); pollTimer=setInterval(refreshStatus,5000); }
  });

  init();
})();
