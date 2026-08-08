#!/usr/bin/env python3
"""YAT local simulator — browse packs, tweak params, preview renders.

Wraps the native engine binary (yat-preview). Zero dependencies.

  cd tools/preview && make && python3 simulator.py
  → http://localhost:8737

The 17 real-world example packs now live in the yat-hk/yat-packs community
repo; this repo only carries packs/internal (firmware-embedded) and
packs/examples/render-test.yat-pack.json (the engine-conformance pack). By
default this simulator also lists `../yat-packs/official` (a sibling
checkout) for the real fleet — pass `--packs-dir` to point elsewhere, or
ignore the printed note if you don't have that checkout and just want the
local packs.

Fixtures: fixtures/<packid>.<sourceid>.json is used automatically when
present (searched here and in the sibling yat-packs checkout's fixtures/);
otherwise the Live checkbox fetches real APIs through the engine.
"""
import argparse
import http.server
import json
import os
import subprocess
import tempfile
import urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
BINARY = os.path.join(HERE, "yat-preview")
# The 17 real-world example packs moved to the yat-hk/yat-packs community
# repo; this core repo now only carries packs/internal (firmware-embedded,
# never user-installed) and packs/examples/render-test.yat-pack.json (the
# engine-conformance pack). The fleet's real packs are found via --packs-dir
# below, a sibling checkout of yat-packs.
PACK_DIRS = [
    os.path.join(ROOT, "packs", "examples"),
    os.path.join(ROOT, "packs", "internal"),
]
DEFAULT_PACKS_DIR = os.path.join(ROOT, "..", "yat-packs", "official")
FIXTURES_LOCAL = os.path.join(HERE, "fixtures")
DEFAULT_FIXTURES_SIBLING = os.path.join(ROOT, "..", "yat-packs", "fixtures")
FIXTURE_ALIASES = {}
PORT = 8737

# Populated by main() from --packs-dir: pack id (filename stem) -> full path.
PACKS = {}
# Fixture directories to search, in order: this repo's own fixtures first
# (render-test/internal), then the sibling yat-packs checkout's fixtures/.
FIXTURE_DIRS = [FIXTURES_LOCAL, DEFAULT_FIXTURES_SIBLING]

PAGE = """<!doctype html><meta charset="utf-8"><title>YAT Simulator</title>
<style>
 body{font-family:ui-monospace,Menlo,monospace;margin:0;display:flex;height:100vh;background:#e8e4da}
 #side{width:340px;min-width:340px;overflow-y:auto;background:#f7f4ec;border-right:2px solid #222;padding:16px}
 #main{flex:1;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:12px;padding:24px}
 h1{font-size:16px;margin:0 0 12px}
 select,input[type=text],input[type=number],input[type=date]{width:100%;box-sizing:border-box;margin:2px 0 10px;padding:6px;border:1px solid #999;background:#fff;font:inherit}
 label{font-size:12px;font-weight:bold}
 .desc{font-size:11px;color:#666;margin:-8px 0 8px}
 button{padding:8px 20px;font:inherit;font-weight:bold;background:#222;color:#f7f4ec;border:0;cursor:pointer}
 #frame{background:#fff;border:14px solid #1a1a1a;border-radius:10px;box-shadow:0 8px 30px rgba(0,0,0,.35)}
 #shot{display:block;width:800px;height:480px;image-rendering:pixelated}
 #log{white-space:pre-wrap;font-size:11px;color:#833;max-width:820px;max-height:140px;overflow-y:auto}
 .chk{margin:8px 0}
 #meta{font-size:11px;color:#555}
</style>
<div id=side>
 <h1>YAT 日 simulator</h1>
 <label>Pack</label><select id=pack onchange=loadPack()></select>
 <div id=form></div>
 <div class=chk><label><input type=checkbox id=live> Live fetch (real APIs)</label></div>
 <button onclick=render()>Render ↻</button>
 <p id=meta></p>
</div>
<div id=main>
 <div id=frame><img id=shot alt="render appears here"></div>
 <div id=log></div>
</div>
<script>
let packs=[],cur=null;
async function init(){
 packs=await (await fetch('/api/packs')).json();
 packs.sort((a,b)=>(a.id==='hko-now'?-1:b.id==='hko-now'?1:0));
 const s=document.getElementById('pack');
 s.innerHTML=packs.map(p=>`<option value="${p.id}">${p.id} — ${p.name}</option>`).join('');
 loadPack();
}
async function loadPack(){
 const id=document.getElementById('pack').value;
 cur=await (await fetch('/api/pack?id='+id)).json();
 const props=(cur.params&&cur.params.properties)||{};
 let h='';
 for(const [k,p] of Object.entries(props)){
  const title=(p.title||k), dflt=p.default!==undefined?p.default:'';
  h+=`<label>${title}</label>`;
  if(p.enum){
   const titles=p.enum_titles||[];
   h+=`<select data-param="${k}" data-type="${p.type}">`+p.enum.map((v,i)=>{
    const t=titles[i]?((titles[i]['zh-Hant']?titles[i]['zh-Hant']+' ':'')+titles[i].en):v;
    return `<option value="${v}" ${v===dflt?'selected':''}>${t}</option>`}).join('')+'</select>';
  } else if(p.type==='boolean'){
   h+=`<div class=chk><input type=checkbox data-param="${k}" data-type="boolean" ${dflt===true?'checked':''}></div>`;
  } else if(p.type==='array'){
   h+=`<input type=text data-param="${k}" data-type="array" value='${JSON.stringify(dflt)}'>`;
   h+=`<div class=desc>JSON array</div>`;
  } else {
   const it=(p.format==='date')?'date':(p.type==='string'?'text':'number');
   h+=`<input type=${it} data-param="${k}" data-type="${p.type}" value="${dflt}">`;
  }
  if(p.description)h+=`<div class=desc>${p.description}</div>`;
 }
 document.getElementById('form').innerHTML=h;
 render();
}
function paramValues(){
 const o={};
 document.querySelectorAll('[data-param]').forEach(el=>{
  const k=el.dataset.param,t=el.dataset.type;
  if(t==='boolean')o[k]=el.checked;
  else if(t==='array'){try{o[k]=JSON.parse(el.value)}catch(e){}}
  else if(t==='number'||t==='integer')o[k]=Number(el.value);
  else o[k]=el.value;
 });
 return o;
}
async function render(){
 const id=document.getElementById('pack').value;
 const live=document.getElementById('live').checked?'1':'0';
 const q='/api/render?id='+id+'&live='+live+'&params='+encodeURIComponent(JSON.stringify(paramValues()))+'&t='+Date.now();
 const t0=performance.now();
 const r=await fetch(q);
 if(r.ok){
  document.getElementById('shot').src=URL.createObjectURL(await r.blob());
  document.getElementById('log').textContent='';
  document.getElementById('meta').textContent='rendered in '+Math.round(performance.now()-t0)+' ms · engine v0.1 subset';
 } else {
  document.getElementById('log').textContent=await r.text();
 }
}
init();
</script>"""


def discover_packs(pack_dirs):
    """Map pack id (filename stem) -> path, across all pack_dirs. First dir
    in the list wins on an id collision."""
    out = {}
    for d in pack_dirs:
        if not os.path.isdir(d):
            continue
        for name in sorted(os.listdir(d)):
            if name.endswith(".yat-pack.json"):
                pid = name[: -len(".yat-pack.json")]
                out.setdefault(pid, os.path.join(d, name))
    return out


def find_fixture(pack_id, sid, exts=("json", "xml", "csv")):
    for d in FIXTURE_DIRS:
        for ext in exts:
            fx = os.path.join(d, f"{pack_id}.{sid}.{ext}")
            if os.path.isfile(fx):
                return fx
    return None


def list_packs():
    out = []
    for pid, path in PACKS.items():
        try:
            d = json.load(open(path, encoding="utf-8"))
            out.append({"id": d["id"], "name": d.get("name", {}).get("en", "")})
        except Exception:
            pass
    return out


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):  # quiet
        pass

    def send(self, code, body, ctype="text/plain; charset=utf-8"):
        data = body if isinstance(body, bytes) else body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        q = urllib.parse.parse_qs(u.query)
        if u.path == "/":
            return self.send(200, PAGE, "text/html; charset=utf-8")
        if u.path == "/api/packs":
            return self.send(200, json.dumps(list_packs()), "application/json")
        if u.path == "/api/pack":
            pid = q.get("id", [""])[0]
            path = PACKS.get(pid)
            if not path or not os.path.isfile(path):
                return self.send(404, "no such pack")
            return self.send(200, open(path, encoding="utf-8").read(), "application/json")
        if u.path == "/api/render":
            pid = q.get("id", [""])[0]
            params = q.get("params", ["{}"])[0]
            live = q.get("live", ["0"])[0] == "1"
            path = PACKS.get(pid)
            if not path or not os.path.isfile(path):
                return self.send(404, "no such pack")
            pack = json.load(open(path, encoding="utf-8"))
            with tempfile.TemporaryDirectory() as td:
                png = os.path.join(td, "out.png")
                cmd = [BINARY, path, "--params", params, "--out", png]
                if live:
                    cmd += ["--live"]  # no --doc: live means live (fixtures would win)
                else:
                    for src in pack.get("data", {}).get("sources", []):
                        sid = src.get("id", "")
                        fx = find_fixture(pid, sid)
                        if fx:
                            cmd += ["--doc", f"{sid}={fx}"]
                r = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
                if r.returncode != 0 or not os.path.isfile(png):
                    return self.send(500, r.stderr or "render failed")
                return self.send(200, open(png, "rb").read(), "image/png")
        return self.send(404, "not found")


def main():
    ap = argparse.ArgumentParser(description="YAT local simulator — browse packs, tweak params, preview renders.")
    ap.add_argument("--packs-dir", default=DEFAULT_PACKS_DIR,
                    help="directory of real-world packs to list alongside packs/internal and "
                         f"packs/examples (default: {DEFAULT_PACKS_DIR}, a sibling yat-packs "
                         "checkout's official/ directory — clone github.com/yat-hk/yat-packs "
                         "next to this repo, or pass a path of your own). Missing is not fatal: "
                         "the simulator falls back to the local packs only.")
    ap.add_argument("--port", type=int, default=PORT, help=f"port to serve on (default: {PORT})")
    args = ap.parse_args()

    if not os.path.isfile(BINARY):
        raise SystemExit("build first: cd tools/preview && make")

    pack_dirs = list(PACK_DIRS)
    if os.path.isdir(args.packs_dir):
        pack_dirs.append(args.packs_dir)
    else:
        print(f"  (note) --packs-dir {args.packs_dir!r} not found — listing "
              "packs/internal and packs/examples only. Clone github.com/yat-hk/yat-packs "
              "next to this repo for the full fleet.")
    global PACKS
    PACKS = discover_packs(pack_dirs)

    fixtures_sibling = os.path.join(args.packs_dir, "..", "fixtures") \
        if args.packs_dir != DEFAULT_PACKS_DIR else DEFAULT_FIXTURES_SIBLING
    global FIXTURE_DIRS
    FIXTURE_DIRS = [FIXTURES_LOCAL, fixtures_sibling]

    print(f"YAT simulator → http://localhost:{args.port}  ({len(PACKS)} pack(s))")
    http.server.ThreadingHTTPServer(("127.0.0.1", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
