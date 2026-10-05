#pragma once
// Renders a self-contained HTML replay viewer from an Engine trace JSON.
#include <string>

#include <nlohmann/json.hpp>

namespace fy {

inline std::string render_replay_html(const nlohmann::json& trace) {
  std::string data = trace.dump();
  // Make the JSON safe to embed inside a <script> tag.
  std::string safe;
  safe.reserve(data.size());
  for (size_t i = 0; i < data.size(); ++i) {
    if (data[i] == '<' && i + 1 < data.size() && data[i + 1] == '/') {
      safe += "<\\/";
      ++i;
    } else {
      safe += data[i];
    }
  }

  static const char* kTemplate = R"HTML(<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>散樱乱武 回放</title>
<style>
:root{--bg:#14171c;--panel:#1e232b;--panel2:#262c36;--line:#39414d;--fg:#e8edf4;--dim:#9aa6b6;
--p0:#5aa9ff;--p1:#ff7a7a;--hi:#ffd166;--good:#7bd88f;}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:14px/1.5 system-ui,"Noto Sans CJK SC",sans-serif}
header{position:sticky;top:0;background:var(--panel);border-bottom:1px solid var(--line);padding:10px 16px;z-index:5}
.row{display:flex;align-items:center;gap:12px;flex-wrap:wrap}
h1{font-size:16px;margin:0 12px 0 0;font-weight:600}
.badge{background:var(--panel2);border:1px solid var(--line);border-radius:999px;padding:2px 10px;font-size:12px}
.badge.active{background:var(--hi);color:#221;border-color:var(--hi);font-weight:600}
.result{margin-left:auto;font-weight:600;color:var(--hi)}
.controls{gap:6px;margin-top:8px}
button{background:var(--panel2);color:var(--fg);border:1px solid var(--line);border-radius:6px;padding:4px 10px;cursor:pointer}
button:hover{background:#313947}
input[type=range]{flex:1;min-width:180px;accent-color:var(--hi)}
main{display:grid;grid-template-columns:1fr 1fr;gap:12px;padding:12px 16px 40px}
.panel{background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:10px 12px}
.panel h2{font-size:13px;margin:0 0 8px;color:var(--dim);font-weight:600;letter-spacing:.04em}
.stats{display:flex;gap:14px;font-size:15px;margin-bottom:8px;flex-wrap:wrap}
.stats b{color:var(--fg)}
.stat{display:flex;gap:4px;align-items:baseline}
.stat .k{color:var(--dim);font-size:12px}
.zone{margin-top:6px}
.zone .zt{color:var(--dim);font-size:12px;margin-bottom:3px}
.chips{display:flex;flex-wrap:wrap;gap:4px}
.chip{background:var(--panel2);border:1px solid var(--line);border-radius:5px;padding:1px 6px;font-size:12px}
.chip.attack{border-color:#7a3a3a}.chip.action{border-color:#3a5a7a}.chip.enhance{border-color:#3a6a4a}
.chip .used{color:var(--hi);font-weight:600}
.chip .cr{color:var(--good)}
.dim{color:var(--dim)}
.p0{color:var(--p0)}.p1{color:var(--p1)}
.stack{display:flex;flex-direction:column-reverse;gap:4px}
.stack .top{outline:2px solid var(--hi)}
.decision{grid-column:1/-1}
.opts{display:flex;flex-wrap:wrap;gap:6px;margin-top:6px}
.opt{background:var(--panel2);border:1px solid var(--line);border-radius:6px;padding:3px 8px;font-size:12px}
.opt.chosen{background:#3a4a2a;border-color:var(--good);font-weight:600}
.center{grid-column:1/-1;text-align:center}
details summary{cursor:pointer;color:var(--dim)}
@media(max-width:820px){main{grid-template-columns:1fr}}
</style>
</head>
<body>
<header>
  <div class="row">
    <h1>散樱乱武 · 回放</h1>
    <span class="badge" id="turn"></span>
    <span class="badge" id="phase"></span>
    <span class="badge active" id="active"></span>
    <span class="result" id="result"></span>
  </div>
  <div class="row controls">
    <button onclick="go(0)">⏮</button>
    <button onclick="go(idx-1)">◀ 上一步</button>
    <button onclick="go(idx+1)">下一步 ▶</button>
    <button onclick="go(1e9)">⏭</button>
    <input type="range" id="slider" min="0" value="0" oninput="go(+this.value)">
    <span class="badge" id="counter"></span>
  </div>
</header>
<main>
  <section class="panel" id="board">
    <h2>场面</h2>
    <div class="stats" id="boardstats"></div>
    <div class="zone"><div class="zt">调用栈 / 对应栈</div><div class="stack" id="stack"></div></div>
  </section>
  <section class="panel decision">
    <h2>当前决策</h2>
    <div id="prompt" class="dim"></div>
    <div class="opts" id="opts"></div>
  </section>
  <section class="panel" id="p0panel"></section>
  <section class="panel" id="p1panel"></section>
</main>
<script>
const DATA = __DATA__;
const frames = DATA.frames || [];
let idx = 0;
const $ = id => document.getElementById(id);
function esc(s){return String(s==null?'':s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));}
function chip(c){
  if(!c) return '';
  const cls = c.type ? c.type : '';
  let inner = esc(c.name||'?');
  if(c.used) inner += ' <span class="used">使用后</span>';
  if(c.crystals!==undefined) inner += ' <span class="cr">❀'+c.crystals+'</span>';
  return '<span class="chip '+cls+'" title="'+esc(c.set||'')+'">'+inner+'</span>';
}
function chips(list){return (list||[]).map(chip).join('') || '<span class="dim">—</span>';}
function playerPanel(p, pi){
  return '<h2 class="p'+pi+'">玩家 '+(pi+1)+' '+esc((p.sets||[]).join(' / '))+'</h2>'
   + '<div class="stats">'
   + '<span class="stat"><span class="k">命</span><b>'+p.life+'</b></span>'
   + '<span class="stat"><span class="k">装</span><b>'+p.aura+'</b></span>'
   + '<span class="stat"><span class="k">气</span><b>'+p.flare+'</b></span>'
   + '<span class="stat"><span class="k">集中</span><b>'+p.vigor+'</b></span>'
   + (p.cower?'<span class="badge">畏缩</span>':'')
   + (p.cannotRespond?'<span class="badge">不能再对应</span>':'')
   + '</div>'
   + '<div class="zone"><div class="zt">手牌 ('+(p.hand||[]).length+')</div><div class="chips">'+chips(p.hand)+'</div></div>'
   + '<div class="zone"><div class="zt">切札</div><div class="chips">'+chips(p.special)+'</div></div>'
   + '<div class="zone"><div class="zt">付与区</div><div class="chips">'+chips(p.enhance)+'</div></div>'
   + '<div class="zone"><div class="zt">弃牌堆 ('+(p.discard||[]).length+')</div><div class="chips">'+chips(p.discard.slice(-12))+'</div></div>'
   + '<div class="zone"><div class="zt">盖牌堆 ('+(p.cover||[]).length+')</div><div class="chips">'+chips(p.cover)+'</div></div>'
   + '<div class="zone"><details><summary>牌山 ('+(p.deck||[]).length+')</summary><div class="chips">'+chips(p.deck)+'</div></details></div>';
}
function render(){
  const f = frames[idx] || {state:{},options:[]};
  const s = f.state || {};
  $('turn').textContent = '第 '+s.turn+' 回合';
  $('phase').textContent = ({setup:'准备构筑',start:'准备阶段',main:'主要阶段',cover:'盖伏阶段',end:'结束阶段'})[s.phase] || s.phase || '';
  $('active').textContent = (s.active===0?'玩家1 行动':s.active===1?'玩家2 行动':'');
  $('result').textContent = (DATA.result&&DATA.result.text)||'';
  $('counter').textContent = (idx+1)+' / '+frames.length;
  $('slider').max = Math.max(0, frames.length-1);
  $('slider').value = idx;
  $('boardstats').innerHTML =
      '<span class="stat"><span class="k">距</span><b>'+s.distance+'</b></span>'
    + '<span class="stat"><span class="k">虚</span><b>'+s.dust+'</b></span>'
    + '<span class="stat"><span class="k">近身距</span><b>'+s.nearDistance+'</b></span>';
  const stack = s.stack||[];
  $('stack').innerHTML = stack.length ? stack.map((e,i)=>'<div class="'+(i===stack.length-1?'top':'')+'">'+chip(e)+' <span class="dim">P'+((e.owner|0)+1)+'</span></div>').join('') : '<span class="dim">（空）</span>';
  $('p0panel').innerHTML = playerPanel((s.players||[])[0]||{}, 0);
  $('p1panel').innerHTML = playerPanel((s.players||[])[1]||{}, 1);
  $('prompt').textContent = (f.prompt||f.label||'') + (f.player!==undefined?('　[玩家'+(f.player+1)+']'):'');
  const chosen = f.choice||[];
  $('opts').innerHTML = (f.options||[]).map(o=>{
    const c = chosen.includes(o.label) ? ' chosen' : '';
    const dis = o.enabled===false ? ' opacity:.4' : '';
    return '<span class="opt'+c+'" style="'+dis+'">'+esc(o.label)+'</span>';
  }).join('') || '<span class="dim">—</span>';
}
function go(i){ idx = Math.max(0, Math.min(frames.length-1, i)); render(); }
document.addEventListener('keydown', e=>{
  if(e.key==='ArrowRight') go(idx+1);
  else if(e.key==='ArrowLeft') go(idx-1);
  else if(e.key==='Home') go(0);
  else if(e.key==='End') go(1e9);
});
go(0);
</script>
</body>
</html>
)HTML";

  std::string html = kTemplate;
  auto pos = html.find("__DATA__");
  if (pos != std::string::npos) html.replace(pos, 8, safe);
  return html;
}

}  // namespace fy
