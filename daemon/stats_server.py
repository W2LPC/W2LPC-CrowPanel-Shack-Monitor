from flask import Flask, jsonify, render_template_string, Response
import psutil, time, math, requests, threading

app = Flask(__name__)

# ── CPU cache — polled every 2s in background with a proper interval ──
_cpu_cache = {'pct':0.0,'cores':[],'temp':0.0}
def _cpu_poll():
    while True:
        try:
            pct   = psutil.cpu_percent(interval=2)
            cores = psutil.cpu_percent(interval=None, percpu=True)
            t = 0.0
            try:
                tmp = psutil.sensors_temperatures()
                if tmp:
                    for k,v in tmp.items():
                        if v: t = float(v[0].current); break
            except: pass
            _cpu_cache['pct']   = round(pct, 1)
            _cpu_cache['cores'] = [round(float(c), 1) for c in cores]
            _cpu_cache['temp']  = round(t, 1)
        except: pass

# ── GPU cache — updated every 5s in background ──
_gpu_cache = {'u':0.0,'t':0.0,'vu':0.0,'vt':0.0}
def _gpu_poll():
    import GPUtil
    while True:
        try:
            g = GPUtil.getGPUs()
            if g:
                def sf2(v):
                    try:
                        f=float(v); return 0.0 if (math.isnan(f) or math.isinf(f)) else f
                    except: return 0.0
                _gpu_cache['u']  = sf2(g[0].load*100)         if g[0].load        is not None else 0.0
                _gpu_cache['t']  = sf2(g[0].temperature)      if g[0].temperature is not None else 0.0
                _gpu_cache['vu'] = sf2(g[0].memoryUsed/1024)  if g[0].memoryUsed  is not None else 0.0
                _gpu_cache['vt'] = sf2(g[0].memoryTotal/1024) if g[0].memoryTotal is not None else 0.0
        except: pass
        time.sleep(5)

threading.Thread(target=_cpu_poll, daemon=True).start()
threading.Thread(target=_gpu_poll, daemon=True).start()

_net_last  = psutil.net_io_counters()
_net_time  = time.time()
_disk_last = psutil.disk_io_counters()
_disk_time = time.time()

IGNORE = {'System Idle Process','System','Registry','Memory Compression'}

def sf(v):
    try:
        f = float(v)
        return 0.0 if (math.isnan(f) or math.isinf(f)) else f
    except: return 0.0

def get_gpu():
    return _gpu_cache['u'], _gpu_cache['t'], _gpu_cache['vu'], _gpu_cache['vt']

def get_net():
    global _net_last,_net_time
    c=psutil.net_io_counters(); now=time.time()
    dt=max(0.1,now-_net_time)
    up=max(0,(c.bytes_sent-_net_last.bytes_sent)/dt/1048576)
    dn=max(0,(c.bytes_recv-_net_last.bytes_recv)/dt/1048576)
    _net_last=c; _net_time=now
    return round(up,2),round(dn,2)

def get_disk():
    global _disk_last,_disk_time
    c=psutil.disk_io_counters(); now=time.time()
    dt=max(0.1,now-_disk_time)
    r=max(0,(c.read_bytes -_disk_last.read_bytes )/dt/1048576)
    w=max(0,(c.write_bytes-_disk_last.write_bytes)/dt/1048576)
    _disk_last=c; _disk_time=now
    return round(r,1),round(w,1)

def get_uptime():
    s=int(time.time()-psutil.boot_time())
    return f"{s//86400}d {(s%86400)//3600:02}:{(s%3600)//60:02}:{s%60:02}"

def get_stats_data():
    cpu   = _cpu_cache['pct']
    cores = _cpu_cache['cores']
    cpu_t = _cpu_cache['temp']
    ram=psutil.virtual_memory()
    dr,dw=get_disk()
    nu,nd=get_net()
    gu,gt,vu,vt=get_gpu()
    procs=[]
    for p in psutil.process_iter(['name','pid','cpu_percent','memory_info','status']):
        try:
            if p.info['name'] in IGNORE: continue
            procs.append({'name':p.info['name'],'pid':p.info['pid'],
                'cpu':round(sf(p.info['cpu_percent']),1),
                'ram':int((p.info['memory_info'].rss if p.info['memory_info'] else 0)/1048576),
                'status':p.info['status'] or 'unknown'})
        except: pass
    procs.sort(key=lambda x:x['cpu'],reverse=True)
    return {
        'cpu_pct':round(sf(cpu),1),'cpu_temp':round(sf(cpu_t),1),
        'cpu_cores':[round(sf(c),1) for c in cores],
        'ram_used':round(ram.used/1073741824,1),'ram_free':round(ram.available/1073741824,1),
        'ram_total':round(ram.total/1073741824,1),'ram_pct':round(ram.percent,1),
        'gpu_pct':round(sf(gu),1),'gpu_temp':round(sf(gt),1),
        'vram_used':round(sf(vu),1),'vram_total':round(sf(vt),1),
        'disk_read':dr,'disk_write':dw,'net_up':nu,'net_down':nd,
        'uptime':get_uptime(),'processes':procs[:5]
    }

def get_bands_data():
    try:
        r = requests.get('https://www.hamqsl.com/solarxml.php', timeout=8, verify=False)
        if r.status_code == 200:
            xml = r.text
            def xv(tag):
                s=xml.find(f'<{tag}>'); e=xml.find(f'</{tag}>')
                return xml[s+len(tag)+2:e].strip() if s>=0 and e>=0 else '---'
            def xb(name,t):
                search=f'<band name="{name}" time="{t}">'
                s=xml.find(search); e=xml.find('</band>',s)
                return xml[s+len(search):e].strip() if s>=0 and e>=0 else '---'
            return {
                'sfi':xv('solarflux'),'aindex':xv('aindex'),
                'kindex':xv('kindex'),'xray':xv('xray'),
                'kindexnt':xv('kindexnt'),
                'bands':{
                    '80m': {'day':xb('80m-40m','day'),'night':xb('80m-40m','night')},
                    '40m': {'day':xb('80m-40m','day'),'night':xb('80m-40m','night')},
                    '20m': {'day':xb('30m-20m','day'),'night':xb('30m-20m','night')},
                    '15m': {'day':xb('17m-15m','day'),'night':xb('17m-15m','night')},
                    '10m': {'day':xb('12m-10m','day'),'night':xb('12m-10m','night')},
                    '6m':  {'day':xb('6m-4m','day'),  'night':xb('6m-4m','night')},
                }
            }
    except: pass
    return {}

_bands_cache = {}
_bands_time  = 0

@app.route('/stats')
def stats():
    return jsonify(get_stats_data())

@app.route('/bands')
def bands():
    global _bands_cache, _bands_time
    if time.time() - _bands_time > 300:
        _bands_cache = get_bands_data()
        _bands_time = time.time()
    return jsonify(_bands_cache)

@app.route('/')
@app.route('/display')
def display():
    return render_template_string(DASHBOARD_HTML)

DASHBOARD_HTML = '''<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>W2LPC System Monitor</title>
<style>
  * { margin:0; padding:0; box-sizing:border-box; }
  body { background:#080c14; color:#e8f4ff; font-family:'Segoe UI',Arial,sans-serif; font-size:13px; min-height:100vh; }
  .topbar { background:#0a1525; border-bottom:1px solid #1e3a5a; padding:8px 16px; display:flex; align-items:center; justify-content:space-between; }
  .topbar .left { display:flex; flex-direction:column; }
  .topbar .date { font-size:11px; color:#4a6a8a; }
  .topbar .time { font-size:22px; font-weight:bold; color:#e8f4ff; line-height:1.1; }
  .topbar .center { font-size:15px; color:#4a8fc0; font-weight:600; text-align:center; }
  .topbar .right { text-align:right; font-size:11px; color:#4a6a8a; }
  .topbar .callsign { font-size:12px; color:#4a6a8a; }
  .topbar .uptime { font-size:11px; color:#4a6a8a; }
  .tabs { display:flex; background:#0d1929; border-bottom:1px solid #1e3a5a; }
  .tab { padding:8px 24px; cursor:pointer; color:#4a6a8a; font-size:12px; font-weight:600; letter-spacing:1px; border-bottom:2px solid transparent; transition:all 0.2s; }
  .tab.active { color:#e8f4ff; border-bottom:2px solid #4a8fc0; }
  .tab:hover { color:#e8f4ff; }
  .page { display:none; padding:12px; }
  .page.active { display:block; }
  .row { display:flex; gap:8px; margin-bottom:8px; }
  .card { background:#0d1929; border:1px solid #1e3a5a; border-radius:6px; padding:10px; flex:1; }
  .card-label { font-size:10px; color:#4a6a8a; letter-spacing:1px; margin-bottom:4px; }
  .card-label-right { font-size:10px; color:#4a6a8a; float:right; }
  .card-val { font-size:26px; font-weight:bold; color:#e8f4ff; line-height:1.1; }
  .card-sub { font-size:11px; color:#ddaa00; margin-top:2px; }
  .bar-track { background:#1a3a5a; border-radius:2px; height:6px; margin-top:6px; }
  .bar-fill { height:100%; border-radius:2px; transition:width 0.5s; }
  .bar-blue  { background:#4a8fc0; }
  .bar-green { background:#3aaa5a; }
  .bar-orange{ background:#ff8c00; }
  .bar-purple{ background:#8a5ad0; }
  .cores-grid { display:grid; grid-template-columns:repeat(6,1fr); gap:4px; margin-top:6px; }
  .core { text-align:center; }
  .core-bar { background:#1a3a5a; border-radius:2px; height:40px; position:relative; display:flex; align-items:flex-end; }
  .core-fill { background:#4a8fc0; border-radius:2px; width:100%; transition:height 0.5s; }
  .core-lbl { font-size:9px; color:#4a6a8a; margin-top:2px; }
  .proc-table { width:100%; border-collapse:collapse; font-size:11px; }
  .proc-table th { color:#4a6a8a; text-align:left; padding:3px 6px; font-weight:600; border-bottom:1px solid #1e3a5a; }
  .proc-table td { padding:3px 6px; border-bottom:1px solid #0a1525; }
  .proc-table tr:nth-child(even) td { background:#0a1525; }
  .band-table { width:100%; border-collapse:collapse; font-size:12px; }
  .band-table th { color:#4a6a8a; text-align:left; padding:6px 10px; font-weight:600; border-bottom:1px solid #1e3a5a; font-size:10px; letter-spacing:1px; }
  .band-table td { padding:8px 10px; border-bottom:1px solid #0d1929; }
  .band-table tr:nth-child(even) td { background:#0a1525; }
  .band-name { font-size:18px; font-weight:bold; color:#e8f4ff; }
  .cond-excellent { color:#3aaa5a; font-weight:600; }
  .cond-good      { color:#4a8fc0; font-weight:600; }
  .cond-fair      { color:#ddaa00; font-weight:600; }
  .cond-poor      { color:#ff8c00; font-weight:600; }
  .cond-none      { color:#4a6a8a; }
  .solar-cards { display:grid; grid-template-columns:repeat(4,1fr); gap:8px; margin-bottom:8px; }
  .solar-val { font-size:28px; font-weight:bold; margin:6px 0 2px; }
  .solar-sub { font-size:10px; letter-spacing:1px; }
  .green  { color:#3aaa5a; }
  .blue   { color:#4a8fc0; }
  .yellow { color:#ddaa00; }
  .orange { color:#ff8c00; }
  .red    { color:#ff3333; }
  .ram-ring { position:relative; width:80px; height:80px; margin:4px auto; }
  .ram-ring svg { transform:rotate(-90deg); }
  .ram-ring-val { position:absolute; top:50%; left:50%; transform:translate(-50%,-50%); text-align:center; font-size:13px; font-weight:bold; color:#e8f4ff; line-height:1.1; }
  .footer { text-align:center; padding:10px; font-size:10px; color:#2a4a6a; border-top:1px solid #1e3a5a; margin-top:8px; }
</style>
</head>
<body>
<div class="topbar">
  <div class="left">
    <span class="date" id="date">--- --- -- ----</span>
    <span class="time" id="clock">00:00:00</span>
  </div>
  <div class="center" id="weather">-- --</div>
  <div class="right">
    <div class="callsign">W2LPC &nbsp; FN20in09</div>
    <div class="uptime" id="uptime">UP: ---</div>
  </div>
</div>
<div class="tabs">
  <div class="tab active" onclick="showTab(\'monitor\')">SYSTEM MONITOR</div>
  <div class="tab" onclick="showTab(\'bands\')">BAND CONDITIONS</div>
</div>

<div id="page-monitor" class="page active">
  <div class="row">
    <div class="card">
      <div class="card-label">CPU <span class="card-label-right">Intel i7-8700</span></div>
      <div class="card-val"><span id="cpu-pct">0</span>%</div>
      <div class="card-sub">TEMP: <span id="cpu-temp">0</span>C</div>
      <div class="bar-track"><div class="bar-fill bar-blue" id="cpu-bar" style="width:0%"></div></div>
      <div class="cores-grid" id="cores-grid"></div>
    </div>
    <div class="card">
      <div class="card-label">RAM <span class="card-label-right">64 GB DDR4</span></div>
      <div class="ram-ring">
        <svg width="80" height="80" viewBox="0 0 80 80">
          <circle cx="40" cy="40" r="34" fill="none" stroke="#0a1e30" stroke-width="10"/>
          <circle cx="40" cy="40" r="34" fill="none" stroke="#4a8fc0" stroke-width="10"
            stroke-dasharray="213.6" id="ram-ring-fill" stroke-dashoffset="213.6"/>
        </svg>
        <div class="ram-ring-val"><span id="ram-gb">--</span><br>GB</div>
      </div>
      <div style="font-size:11px;color:#4a6a8a;margin-top:4px">
        USED: <span style="color:#e8f4ff" id="ram-used">--</span> GB &nbsp;
        FREE: <span style="color:#3aaa5a" id="ram-free">--</span> GB
      </div>
      <div class="bar-track" style="margin-top:6px"><div class="bar-fill bar-blue" id="ram-bar" style="width:0%"></div></div>
      <div style="font-size:10px;color:#4a6a8a;margin-top:3px"><span id="ram-pct">0</span>% utilized</div>
    </div>
    <div class="card">
      <div class="card-label">GPU <span class="card-label-right">GT 730 4GB</span></div>
      <div class="card-val"><span id="gpu-pct">0</span>%</div>
      <div class="card-sub">TEMP: <span id="gpu-temp">0</span>C</div>
      <div class="bar-track"><div class="bar-fill bar-purple" id="gpu-bar" style="width:0%"></div></div>
      <div style="font-size:10px;color:#4a6a8a;margin-top:6px">VRAM</div>
      <div class="bar-track"><div class="bar-fill bar-purple" id="vram-bar" style="width:0%"></div></div>
      <div style="font-size:10px;color:#4a6a8a;margin-top:3px"><span id="vram-val">-- / --</span> GB</div>
    </div>
  </div>
  <div class="row">
    <div class="card">
      <div class="card-label">DISK READ <span class="card-label-right">10TB WD Blue</span></div>
      <div class="card-val green"><span id="disk-read">0</span> <span style="font-size:13px;color:#4a6a8a">MB/s</span></div>
      <div class="bar-track"><div class="bar-fill bar-green" id="dr-bar" style="width:0%"></div></div>
      <div style="margin-top:8px" class="card-label">DISK WRITE</div>
      <div class="card-val orange"><span id="disk-write">0</span> <span style="font-size:13px;color:#4a6a8a">MB/s</span></div>
      <div class="bar-track"><div class="bar-fill bar-orange" id="dw-bar" style="width:0%"></div></div>
    </div>
    <div class="card">
      <div class="card-label">NETWORK <span class="card-label-right">192.168.0.11</span></div>
      <div style="display:flex;gap:20px;margin-top:4px">
        <div>
          <div style="font-size:10px;color:#4a6a8a">UPLOAD</div>
          <div class="card-val blue"><span id="net-up">0.0</span> <span style="font-size:13px;color:#4a6a8a">MB/s</span></div>
          <div class="bar-track"><div class="bar-fill bar-blue" id="nu-bar" style="width:0%"></div></div>
        </div>
        <div>
          <div style="font-size:10px;color:#4a6a8a">DOWNLOAD</div>
          <div class="card-val green"><span id="net-dn">0.0</span> <span style="font-size:13px;color:#4a6a8a">MB/s</span></div>
          <div class="bar-track"><div class="bar-fill bar-green" id="nd-bar" style="width:0%"></div></div>
        </div>
      </div>
    </div>
  </div>
  <div class="card">
    <div class="card-label">TOP PROCESSES</div>
    <table class="proc-table">
      <thead><tr><th>PROCESS</th><th>PID</th><th>CPU%</th><th>RAM</th><th>STATUS</th></tr></thead>
      <tbody id="proc-tbody"></tbody>
    </table>
  </div>
</div>

<!-- BANDS PAGE -->
<div id="page-bands" class="page">
  <div class="solar-cards" id="solar-cards">
    <div class="card"><div class="card-label">SOLAR FLUX INDEX</div><div class="solar-val" id="b-sfi">---</div><div class="solar-sub" id="b-sfi-sub">---</div></div>
    <div class="card"><div class="card-label">A-INDEX</div><div class="solar-val" id="b-ai">--</div><div class="solar-sub" id="b-ai-sub">---</div></div>
    <div class="card"><div class="card-label">K-INDEX</div><div class="solar-val" id="b-ki">--</div><div class="solar-sub" id="b-ki-sub">---</div></div>
    <div class="card"><div class="card-label">X-RAY</div><div class="solar-val" id="b-xr">---</div><div class="solar-sub" id="b-xr-sub">---</div></div>
  </div>
  <div class="card">
    <table class="band-table">
      <thead><tr><th>BAND</th><th>DAY</th><th>NIGHT</th><th>STATUS</th></tr></thead>
      <tbody id="band-tbody"></tbody>
    </table>
  </div>
  <div style="font-size:10px;color:#4a6a8a;margin-top:6px;text-align:right" id="bands-updated"></div>
</div>

<div class="footer">COMPUTER GEEKS OF AMERICA &nbsp;|&nbsp; W2LPC &nbsp;|&nbsp; LPC Systems LLC &nbsp;|&nbsp; ThinkStation P330</div>

<script>
function showTab(name) {
  document.querySelectorAll(\'.tab\').forEach((t,i)=>t.classList.toggle(\'active\',[\'monitor\',\'bands\'][i]===name));
  document.querySelectorAll(\'.page\').forEach(p=>p.classList.remove(\'active\'));
  document.getElementById(\'page-\'+name).classList.add(\'active\');
}
function condClass(c) {
  if(!c||c===\'---\') return \'cond-none\';
  c=c.toLowerCase();
  if(c.includes(\'excell\')) return \'cond-excellent\';
  if(c.includes(\'good\'))   return \'cond-good\';
  if(c.includes(\'fair\'))   return \'cond-fair\';
  if(c.includes(\'poor\'))   return \'cond-poor\';
  return \'cond-none\';
}
function sfiColor(v){return v>=150?\'green\':v>=120?\'blue\':v>=90?\'yellow\':\'orange\';}
function aiColor(v) {return v<=7?\'green\':v<=15?\'yellow\':v<=29?\'orange\':\'red\';}
function kiColor(v) {return v<=2?\'green\':v<=4?\'yellow\':\'red\';}
function xrColor(x) {if(!x||x===\'---\')return\'green\';if(x[0]===\'X\')return\'red\';if(x[0]===\'M\')return\'orange\';if(x[0]===\'C\')return\'yellow\';return\'green\';}

function clock() {
  const now=new Date();
  const days=[\'SUN\',\'MON\',\'TUE\',\'WED\',\'THU\',\'FRI\',\'SAT\'];
  const months=[\'JAN\',\'FEB\',\'MAR\',\'APR\',\'MAY\',\'JUN\',\'JUL\',\'AUG\',\'SEP\',\'OCT\',\'NOV\',\'DEC\'];
  document.getElementById(\'date\').textContent=days[now.getDay()]+\' \'+months[now.getMonth()]+\' \'+String(now.getDate()).padStart(2,\'0\')+\' \'+now.getFullYear();
  document.getElementById(\'clock\').textContent=String(now.getHours()).padStart(2,\'0\')+\':\'+String(now.getMinutes()).padStart(2,\'0\')+\':\'+String(now.getSeconds()).padStart(2,\'0\');
}
setInterval(clock,1000); clock();

let _statsCtrl=null;
async function updateStats() {
  if(_statsCtrl) _statsCtrl.abort();
  _statsCtrl=new AbortController();
  try {
    const r=await fetch(\'/stats\',{signal:_statsCtrl.signal});
    const d=await r.json();
    document.getElementById(\'cpu-pct\').textContent=d.cpu_pct;
    document.getElementById(\'cpu-temp\').textContent=d.cpu_temp;
    document.getElementById(\'cpu-bar\').style.width=d.cpu_pct+\'%\';
    document.getElementById(\'uptime\').textContent=\'UP \'+d.uptime;
    const cg=document.getElementById(\'cores-grid\'); cg.innerHTML=\'\';
    (d.cpu_cores||[]).forEach((v,i)=>{
      cg.innerHTML+=\'<div class="core"><div class="core-bar"><div class="core-fill" style="height:\'+v+\'%"></div></div><div class="core-lbl">C\'+(i+1)+\'</div></div>\';
    });
    const rp=d.ram_pct; const circ=213.6;
    document.getElementById(\'ram-ring-fill\').setAttribute(\'stroke-dashoffset\',circ-(circ*rp/100));
    document.getElementById(\'ram-gb\').textContent=Math.round(d.ram_used);
    document.getElementById(\'ram-used\').textContent=d.ram_used;
    document.getElementById(\'ram-free\').textContent=d.ram_free;
    document.getElementById(\'ram-bar\').style.width=rp+\'%\';
    document.getElementById(\'ram-pct\').textContent=rp;
    document.getElementById(\'gpu-pct\').textContent=d.gpu_pct;
    document.getElementById(\'gpu-temp\').textContent=d.gpu_temp;
    document.getElementById(\'gpu-bar\').style.width=d.gpu_pct+\'%\';
    const vp=d.vram_total>0?Math.round(d.vram_used/d.vram_total*100):0;
    document.getElementById(\'vram-bar\').style.width=vp+\'%\';
    document.getElementById(\'vram-val\').textContent=d.vram_used+\' / \'+d.vram_total;
    document.getElementById(\'disk-read\').textContent=d.disk_read;
    document.getElementById(\'disk-write\').textContent=d.disk_write;
    document.getElementById(\'dr-bar\').style.width=Math.min(d.disk_read/3,100)+\'%\';
    document.getElementById(\'dw-bar\').style.width=Math.min(d.disk_write/3,100)+\'%\';
    document.getElementById(\'net-up\').textContent=d.net_up;
    document.getElementById(\'net-dn\').textContent=d.net_down;
    document.getElementById(\'nu-bar\').style.width=Math.min(d.net_up*10,100)+\'%\';
    document.getElementById(\'nd-bar\').style.width=Math.min(d.net_down*10,100)+\'%\';
    const tb=document.getElementById(\'proc-tbody\'); tb.innerHTML=\'\';
    (d.processes||[]).forEach(p=>{
      const ram=p.ram>=1024?(p.ram/1024).toFixed(1)+\' GB\':p.ram+\' MB\';
      tb.innerHTML+=\'<tr><td>\'+p.name+\'</td><td>\'+p.pid+\'</td><td style="color:#4a8fc0">\'+p.cpu+\'%</td><td>\'+ram+\'</td><td style="color:#3aaa5a">\'+(p.status||\'\').toUpperCase()+\'</td></tr>\';
    });
  } catch(e){ if(e.name!==\'AbortError\') console.log(\'stats error\',e); }
}

async function updateBands() {
  try {
    const r=await fetch(\'/bands\'); const d=await r.json();
    if(!d.sfi) return;
    const sfi=parseInt(d.sfi)||0;
    const ai=parseInt(d.aindex)||0;
    const ki=parseInt(d.kindex)||0;
    document.getElementById(\'b-sfi\').innerHTML=\'<span class="\'+sfiColor(sfi)+\'">\'+d.sfi+\'</span>\';
    document.getElementById(\'b-sfi-sub\').className=\'solar-sub \'+sfiColor(sfi);
    document.getElementById(\'b-sfi-sub\').textContent=sfi>=150?\'EXCELLENT\':sfi>=120?\'GOOD\':sfi>=90?\'FAIR\':\'POOR\';
    document.getElementById(\'b-ai\').innerHTML=\'<span class="\'+aiColor(ai)+\'">\'+d.aindex+\'</span>\';
    document.getElementById(\'b-ai-sub\').className=\'solar-sub \'+aiColor(ai);
    document.getElementById(\'b-ai-sub\').textContent=ai<=7?\'QUIET\':ai<=15?\'UNSETTLED\':ai<=29?\'ACTIVE\':\'STORM\';
    document.getElementById(\'b-ki\').innerHTML=\'<span class="\'+kiColor(ki)+\'">\'+d.kindex+\'</span>\';
    document.getElementById(\'b-ki-sub\').className=\'solar-sub \'+kiColor(ki);
    document.getElementById(\'b-ki-sub\').textContent=(d.kindexnt||\'---\').toUpperCase();
    document.getElementById(\'b-xr\').innerHTML=\'<span class="\'+xrColor(d.xray)+\'">\'+d.xray+\'</span>\';
    document.getElementById(\'b-xr-sub\').className=\'solar-sub \'+xrColor(d.xray);
    document.getElementById(\'b-xr-sub\').textContent=d.xray&&d.xray[0]===\'X\'?\'MAJOR FLARE\':d.xray&&d.xray[0]===\'M\'?\'MOD FLARE\':d.xray&&d.xray[0]===\'C\'?\'MINOR FLARE\':\'NORMAL\';
    const bands=[[\'80m\',\'80m\'],[\'40m\',\'40m\'],[\'20m\',\'20m\'],[\'15m\',\'15m\'],[\'10m\',\'10m\'],[\'6m\',\'6m\']];
    const tb=document.getElementById(\'band-tbody\'); tb.innerHTML=\'\';
    bands.forEach(([label,key])=>{
      const b=(d.bands||{})[key]||{};
      const dc=condClass(b.day||\'---\'); const nc=condClass(b.night||\'---\');
      const dx=(sfi>=120&&ki<=2)?\'<span style="color:#3aaa5a;font-size:10px">DX WORTHY</span>\':\'\';;
      tb.innerHTML+=\'<tr><td><span class="band-name">\'+label+\'</span></td><td class="\'+dc+\'">\'+( b.day||\'---\').toUpperCase()+\'</td><td class="\'+nc+\'">\'+( b.night||\'---\').toUpperCase()+\'</td><td>\'+dx+\'</td></tr>\';
    });
    document.getElementById(\'bands-updated\').textContent=\'hamqsl.com — updated \'+new Date().toLocaleTimeString();
  } catch(e){ console.log(\'bands error\',e); }
}

updateStats(); updateBands();
setInterval(updateStats,2000);
setInterval(updateBands,300000);
</script>
</body>
</html>'''
@app.route('/gpu-debug')
def gpu_debug():
    results = {}
    try:
        import GPUtil
        g = GPUtil.getGPUs()
        if g:
            results['gputil'] = {
                'load': g[0].load,
                'temp': g[0].temperature,
                'memUsed': g[0].memoryUsed,
                'memTotal': g[0].memoryTotal
            }
        else:
            results['gputil'] = 'no GPUs found'
    except Exception as e:
        results['gputil_error'] = str(e)
    try:
        import wmi
        w = wmi.WMI(namespace="root\\OpenHardwareMonitor")
        sensors = w.Sensor()
        results['ohm_sensors'] = [s.Name for s in sensors]
    except Exception as e:
        results['ohm_error'] = str(e)
    try:
        import subprocess
        r = subprocess.run(['nvidia-smi','--query-gpu=utilization.gpu,temperature.gpu,memory.used,memory.total','--format=csv,noheader,nounits'], capture_output=True, text=True, timeout=5)
        results['nvidia_smi'] = r.stdout.strip()
        results['nvidia_smi_err'] = r.stderr.strip()
    except Exception as e:
        results['nvidia_smi_error'] = str(e)
    return jsonify(results)
if __name__=='__main__':
    import urllib3; urllib3.disable_warnings()
    app.run(host='0.0.0.0',port=5000,debug=False,use_reloader=False)
