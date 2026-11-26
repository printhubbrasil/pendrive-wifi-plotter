#pragma once
// v1.23: painel de várias plaquinhas. A página de qualquer plaquinha mostra e controla todas as da rede
// (fala direto com o /api de cada uma; a de onde a página veio é chamada por endereço relativo).
const char PAGINA[] PROGMEM = R"HTML(<!doctype html>
<html lang="pt-BR"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Pen Drives de Rede</title>
<style>
:root{--az:#00a8e8;--azc:#e6f6fd;--fu:#f1f4f7;--ca:#fff;--tx:#1d2733;--cz:#6b7785;--li:#e6eaef;--ok:#1aa260;--er:#d93838;--erc:#fde8e8;--am:#c98a00;--amc:#fff4d6;--bt:#e3e8ee}
@media(prefers-color-scheme:dark){:root{--azc:#0d3344;--fu:#12171d;--ca:#1c232c;--tx:#e8edf2;--cz:#93a0ae;--li:#2a333e;--erc:#4a1f1f;--amc:#43350c;--bt:#2c3642}}
*{box-sizing:border-box}body{margin:0;font:16px system-ui,sans-serif;background:var(--fu);color:var(--tx)}
main{max-width:1180px;margin:0 auto;padding:16px}
header{display:flex;align-items:center;gap:12px;flex-wrap:wrap;margin-bottom:14px}
header>div{flex:1;min-width:200px}h1{font-size:22px;margin:0}.sub{color:var(--cz);font-size:14px}
.card{background:var(--ca);border-radius:12px;padding:16px;margin-bottom:14px;box-shadow:0 1px 3px #0001}
.card>b:first-child{display:block;margin-bottom:10px}
.grade{display:grid;grid-template-columns:repeat(auto-fill,minmax(250px,1fr));gap:12px;margin-bottom:14px}
.pl{background:var(--ca);border-radius:12px;padding:14px;border:2px solid transparent;border-left:6px solid var(--cz);cursor:pointer;box-shadow:0 1px 3px #0001;min-width:0}
.pl.foco{border-color:var(--az);border-left-color:var(--az)}.pl.sobre{background:var(--azc);border-style:dashed;border-color:var(--az)}
.pl.ok{border-left-color:var(--ok)}.pl.az{border-left-color:var(--az)}.pl.am{border-left-color:var(--am)}.pl.off{border-left-color:var(--er);opacity:.8}
.pl .t{display:flex;align-items:center;gap:8px}.pl .t b{flex:1;font-size:19px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.pl .s{font-weight:600;margin:4px 0 8px}.ok .s{color:var(--ok)}.az .s{color:var(--az)}.am .s{color:var(--am)}.off .s{color:var(--er)}
.pl p{margin:2px 0;font-size:14px;color:var(--cz)}.pl p.e{color:var(--er);font-weight:600}
.sinal{display:inline-flex;gap:2px;align-items:flex-end;height:14px}.sinal i{width:4px;background:var(--li);border-radius:1px}.sinal i.a{background:var(--cz)}
.tag{font-size:12px;border-radius:6px;padding:2px 6px;background:var(--amc);color:var(--am);font-weight:600}
.esp{height:5px;background:var(--li);border-radius:3px;overflow:hidden;margin:8px 0 2px}.esp i{display:block;height:100%;background:var(--az)}
.cols{display:grid;grid-template-columns:minmax(0,3fr) minmax(0,2fr);gap:14px;align-items:start}
@media(max-width:800px){.cols{grid-template-columns:minmax(0,1fr)}}
@media(max-width:560px){.grade{grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:8px}.pl{padding:10px}.pl .t b{font-size:17px}main{padding:10px}}
#zona{border:2px dashed var(--az);border-radius:12px;padding:30px 12px;text-align:center;cursor:pointer;color:var(--az);font-weight:600}
#zona.sobre{background:var(--azc)}
.chips{display:flex;flex-wrap:wrap;gap:6px;margin-bottom:10px;align-items:center}
.chip{border:1px solid var(--li);background:transparent;color:var(--tx);border-radius:20px;padding:6px 12px}
.chip.sel{background:var(--az);border-color:var(--az);color:#fff;font-weight:600}.chip:disabled{opacity:.45;cursor:default}
.barra{height:8px;background:var(--li);border-radius:4px;overflow:hidden;margin-top:12px;display:none}
.barra i{display:block;height:100%;width:0;background:var(--az)}
#msg{margin-top:10px;font-weight:600;min-height:20px}
.lin{display:flex;align-items:center;gap:8px;padding:10px 0;border-bottom:1px solid var(--li)}
.lin:last-child{border:0}.lin .n{flex:1;word-break:break-all}.lin small{color:var(--cz);white-space:nowrap}
button{font:inherit;border:0;border-radius:8px;padding:8px 12px;background:var(--bt);color:var(--tx);cursor:pointer}
button.v{background:var(--erc);color:var(--er)}button.p{background:var(--az);color:#fff;font-weight:600}
label{display:flex;gap:8px;align-items:center;margin:8px 0}
input[type=text],input[type=password],select{font:inherit;width:100%;padding:8px;border:1px solid var(--li);border-radius:8px;margin:4px 0 10px;background:var(--ca);color:var(--tx)}
details summary{cursor:pointer;font-weight:600}hr{border:0;border-top:1px solid var(--li);margin:16px 0}
.fatos{display:grid;grid-template-columns:auto 1fr;gap:6px 12px;font-size:14px}.fatos span{color:var(--cz)}
.aviso{background:var(--erc);color:var(--er);border-radius:10px;padding:10px 14px;margin-bottom:10px;font-weight:600}
.aviso.am{background:var(--amc);color:var(--am)}
#addprog{padding:10px;border-radius:8px;background:var(--azc);margin-bottom:14px}a{color:var(--az)}
</style></head><body><main>
<header><div><h1>Pen Drives de Rede</h1><div class="sub" id="resumo">carregando…</div></div>
<button class="p" id="btadd">+ Adicionar plaquinha</button></header>
<div id="avisos"></div>
<div class="card" id="addform" style="display:none"><b>Plaquinha nova</b><p style="margin:0 0 4px">Nome dela (ex.: teneth, saga2):</p><input type="text" id="addnome" maxlength="11"><button class="p" id="addok">Começar</button> <button id="addnao">Cancelar</button></div>
<div id="addprog" style="display:none"><b id="addtit"></b><div id="addtxt" style="margin:6px 0"></div><button id="addcanc">Cancelar</button></div>
<p id="addfim" style="font-weight:600;margin:0 0 12px"></p>

<section class="grade" id="grade"></section>

<div class="cols"><div>
<div class="card"><b id="envTit">Enviar arquivos</b>
<div id="zona">Arraste os arquivos aqui<br><small style="font-weight:400">ou toque para escolher (pode ser vários). Vai só para a plaquinha escolhida acima.</small></div>
<input type="file" id="arq" multiple hidden>
<div class="barra" id="barra"><i></i></div>
<div id="msg"></div>
</div>

<div class="card"><div style="display:flex;align-items:center;gap:8px"><b style="flex:1" id="arqTit">Arquivos</b><button class="v" id="limpa" style="font-weight:600">Apagar tudo</button></div><div id="selbar" style="display:flex;align-items:center;gap:8px;margin-top:10px"><label style="flex:1;margin:0"><input type="checkbox" id="todos"> Selecionar todos</label><button class="v" id="apsel" style="display:none"></button></div><div id="lista"></div></div>
</div><div>

<div class="card"><b id="saudeTit">Como está a plaquinha</b><div class="fatos" id="saude"></div></div>

<div class="card"><details id="dcfg"><summary id="cfgTit">Configurações</summary>
<p><b>Nome da plaquinha</b> (vira o endereço <i>nome</i>.local e o nome do pen drive)</p>
<input type="text" id="nome" maxlength="11">
<p><b>Ao trocar o arquivo</b></p>
<select id="modo"><option value="1">Desplugar e plugar o USB (mais garantido)</option>
<option value="0">Só trocar o disco (mais rápido)</option></select>
<button onclick="salvarCfg()">Salvar</button>
<hr>
<p><b>Wi-Fi</b></p>
<select id="redes"><option value="">toque em "Procurar redes"</option></select>
<button onclick="redes()">Procurar redes</button>
<input type="text" id="ssid" placeholder="ou digite o nome da rede" style="margin-top:10px">
<input type="password" id="senha" placeholder="senha">
<button onclick="salvarWifi()">Conectar</button>
<hr>
<p><b>Atualizar o programa só desta</b> (arquivo .bin)</p>
<input type="file" id="fw" accept=".bin"><button onclick="atualizar([foco],$('fw'))">Atualizar</button>
<button onclick="gravacao()">Modo gravação pelo cabo</button>
<hr>
<button class="v" id="esquece">Tirar esta plaquinha do painel</button>
</details></div>

<div class="card"><details><summary>Atualizar o programa de todas</summary>
<p>Manda o mesmo arquivo .bin para todas as plaquinhas que estão no ar, uma de cada vez. Pula a que estiver com a máquina lendo.</p>
<input type="file" id="fwt" accept=".bin"><button onclick="atualizar(Object.keys(P),$('fwt'))">Atualizar todas</button>
<div id="fwlog" style="margin-top:10px;font-size:14px"></div>
</details></div>
</div></div>
</main><script>
const $=i=>document.getElementById(i),h=s=>String(s==null?'':s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
function kb(b){return b>1048576?(b/1048576).toFixed(1).replace('.',',')+' MB':Math.ceil(b/1024)+' KB'}
function dur(s){s=Math.max(0,Math.round(s));return s<60?s+' s':s<3600?Math.floor(s/60)+' min':s<86400?Math.floor(s/3600)+' h '+Math.floor(s%3600/60)+' min':Math.floor(s/86400)+' d '+Math.floor(s%86400/3600)+' h'}
function msg(t,cor){$('msg').textContent=t;$('msg').style.color=cor||''}
function busca(u,o,ms){const c=new AbortController(),t=setTimeout(()=>c.abort(),ms||3500);return fetch(u,Object.assign({signal:c.signal},o||{})).finally(()=>clearTimeout(t))}
function nver(v){const a=String(v||'0').split('.');return (+a[0]||0)*1000+(+a[1]||0)}
const P={};let foco=null,eu=null,alvos=new Set(),marcados=new Set(),nomes=[],addAtivo=false,cfgDe=null,enviando=false;
function guarda(){try{localStorage.pdr=JSON.stringify(Object.values(P).map(p=>({id:p.id,nome:p.nome,ip:p.ip})));localStorage.pdrFoco=foco||''}catch(e){}}
function carrega(){try{JSON.parse(localStorage.pdr||'[]').forEach(x=>{if(x.id&&x.ip)P[x.id]={id:x.id,nome:x.nome||'?',ip:x.ip,base:'http://'+x.ip,on:false,erros:0,visto:0}});foco=localStorage.pdrFoco||null}catch(e){}}
function focar(id){foco=id;alvos=new Set([id]);marcados.clear();cfgDe=null;guarda();pinta()}

// ---- quem está na rede ----
async function descobrir(){
 let d;try{d=await(await busca('/api/placas')).json()}catch(e){return}
 const poe=(x,souEu)=>{
  Object.values(P).forEach(q=>{if(q.ip==x.ip&&q.id!=x.mac)delete P[q.id]});
  const p=P[x.mac]||(P[x.mac]={id:x.mac,on:false,erros:0,visto:0});
  if(!p.e)p.nome=x.nome;p.ip=x.ip;p.eu=souEu;p.base=souEu?'':'http://'+x.ip};
 poe(d.eu,true);eu=d.eu.mac;d.placas.forEach(x=>poe(x,false));
 if(!foco||!P[foco])foco=eu;
 const a=d.adicionar;
 if(a.resta>0){addAtivo=true;$('addprog').style.display='';$('addtit').textContent='Adicionando a plaquinha "'+a.nome+'"';$('addtxt').textContent=a.progresso;$('addfim').textContent='';$('btadd').style.display='none'}
 else{$('addprog').style.display='none';$('btadd').style.display='';if(addAtivo){addAtivo=false;$('addfim').textContent=a.progresso}}
 guarda();
}
async function sonda(p){
 if(p.ocupada)return;
 try{const e=await(await busca(p.base+'/api/estado')).json();p.e=e;p.nome=e.nome;p.on=true;p.erros=0;p.visto=Date.now()}
 catch(x){if(++p.erros>=2)p.on=false}
}
async function tique(){await Promise.all(Object.values(P).map(sonda));pinta()}

// ---- situação de cada uma, em palavras ----
function sit(p){
 if(p.ocupada)return['az','Recebendo…'];
 if(!p.on)return['off','Fora do ar',p.visto?'sem resposta há '+dur((Date.now()-p.visto)/1000):'ainda não respondeu'];
 const e=p.e;
 if(e.pend)return['az','Atualizando o pen drive…'];
 if(e.usbFora)return['az','Voltando para a máquina…'];
 if(e.usbSuspenso)return['am','Porta parada pela máquina','a plaquinha tenta voltar sozinha'];
 if(!e.usbMontado)return['am','Sem máquina','máquina desligada ou cabo solto'];
 if(e.ultLeitura>=0&&e.ultLeitura<15)return['az','Máquina lendo agora'];
 return['ok','Pronta na máquina'];
}
function sinal(r){const n=!r?0:r>-60?4:r>-70?3:r>-80?2:1;let s='<span class="sinal" title="sinal do Wi-Fi '+r+' dBm">';for(let i=1;i<=4;i++)s+='<i style="height:'+(i*3+2)+'px"'+(i<=n?' class="a"':'')+'></i>';return s+'</span>'}
function narq(e){return e.arquivos.filter(a=>a.nome!='_REDE.TXT').length}

function pinta(){
 const L=Object.values(P).sort((a,b)=>(a.nome||'').localeCompare(b.nome||''));
 if(!L.length)return;
 if(!foco||!P[foco])foco=eu||L[0].id;
 [...alvos].forEach(i=>{if(!P[i])alvos.delete(i)});if(!alvos.size)alvos.add(foco);
 const maior=Math.max(0,...L.filter(p=>p.on).map(p=>nver(p.e.versao)));
 // resumo e avisos
 const on=L.filter(p=>p.on),pr=on.filter(p=>sit(p)[0]=='ok'||sit(p)[1]=='Máquina lendo agora').length;
 $('resumo').textContent=L.length+(L.length>1?' plaquinhas':' plaquinha')+' · '+pr+(pr==1?' ligada':' ligadas')+' em máquina'+(L.length-on.length?' · '+(L.length-on.length)+' fora do ar':'');
 let av='';
 L.forEach(p=>{const n=h(p.nome);
  if(!p.on&&p.visto)av+='<div class="aviso">'+n+' saiu do ar há '+dur((Date.now()-p.visto)/1000)+'.</div>';
  else if(p.on){const e=p.e;
   if(e.entregasRuins>0)av+='<div class="aviso">'+n+': '+e.entregasRuins+' arquivo(s) chegaram com erro na máquina. Envie de novo.</div>';
   if(e.total&&e.livre/e.total<.1)av+='<div class="aviso am">'+n+' está quase cheia ('+kb(e.livre)+' livres). Apague arquivos antigos.</div>';
   if(!e.ip&&p.eu)av+='<div class="aviso am">Esta plaquinha ainda não está no Wi-Fi. Abra Configurações e escolha a rede.</div>'}});
 if($('avisos')._h!=av){$('avisos').innerHTML=av;$('avisos')._h=av}
 // cartões
 const G=$('grade');
 [...G.children].forEach(c=>{if(!P[c.dataset.id])c.remove()});
 L.forEach(p=>{
  let c=[...G.children].find(x=>x.dataset.id==p.id);
  if(!c){c=document.createElement('div');c.dataset.id=p.id;c.onclick=()=>focar(p.id);
   c.ondragover=e=>{e.preventDefault();c.classList.add('sobre')};c.ondragleave=()=>c.classList.remove('sobre');
   c.ondrop=e=>{e.preventDefault();e.stopPropagation();c.classList.remove('sobre');focar(p.id);enviar(e.dataTransfer.files)};G.appendChild(c)}
  const s=sit(p),e=p.e;
  let t='<div class="t"><b>'+h(p.nome)+'</b>'+(p.on?sinal(e.rssi):'')+'</div><div class="s">'+s[1]+'</div>';
  if(s[2])t+='<p>'+s[2]+'</p>';
  if(p.on){const n=narq(e);
   t+='<p>'+n+(n==1?' arquivo':' arquivos')+' · '+kb(e.livre)+' livres</p><div class="esp"><i style="width:'+(e.total?Math.round(100*(e.total-e.livre)/e.total):0)+'%"></i></div>';
   t+='<p>última leitura da máquina: '+(e.ultLeitura<0?'nenhuma':'há '+dur(e.ultLeitura))+'</p>';
   t+='<p>'+e.entregasOk+' entregue(s) à máquina</p>';
   if(e.entregasRuins>0)t+='<p class="e">'+e.entregasRuins+' com erro</p>';
   t+='<p>versão '+h(e.versao)+(nver(e.versao)<maior?' <span class="tag">versão antiga</span>':'')+(p.eu?' · esta':'')+'</p>'}
  else t+='<p>'+h(p.ip)+'</p>';
  const cl='pl '+s[0]+(p.id==foco?' foco':'')+(c.classList.contains('sobre')?' sobre':'');
  if(c.className!=cl)c.className=cl;
  if(c._h!=t){c.innerHTML=t;c._h=t}
 });
 if([...G.children].map(c=>c.dataset.id).join()!=L.map(p=>p.id).join())L.forEach(p=>G.appendChild([...G.children].find(x=>x.dataset.id==p.id)));
 detalhe();
}

// ---- a plaquinha escolhida ----
function detalhe(){
 const p=P[foco];if(!p)return;const e=p.e,s=sit(p);
 $('envTit').textContent='Enviar arquivos para a '+p.nome;
 $('arqTit').innerHTML='Arquivos na '+h(p.nome)+' <span id="qtd" style="color:var(--cz);font-weight:400"></span>';
 $('saudeTit').textContent='Como está a '+p.nome;$('cfgTit').textContent='Configurações da '+p.nome;
 $('esquece').style.display=p.eu?'none':'';
 if(!p.on||!e){
  nomes=[];$('selbar').style.display='none';$('limpa').style.display='none';
  $('lista').innerHTML='<p style="color:var(--cz)">'+(e?'Sem resposta desta plaquinha. Confira se ela está ligada e no Wi-Fi.':'procurando…')+'</p>';
  $('saude').innerHTML='<span>Situação</span><b>'+s[1]+'</b><span>Endereço</span><div>'+h(p.ip)+'</div>';return}
  if(cfgDe!=p.id){cfgDe=p.id;$('nome').value=e.nome;$('modo').value=e.replugar?'1':'0'}
 nomes=e.arquivos.map(a=>a.nome).filter(n=>n!='_REDE.TXT');marcados.forEach(n=>{if(!nomes.includes(n))marcados.delete(n)});
 const nA=nomes.length;$('selbar').style.display=nA?'flex':'none';$('qtd').textContent='('+nA+')';$('limpa').onclick=apagarTudo;$('limpa').style.display=nA?'':'none';
 const chave=p.id+JSON.stringify(e.arquivos);
 if($('lista')._h!=chave){$('lista')._h=chave;
  $('lista').innerHTML=e.arquivos.length?'':'<p style="color:var(--cz)">vazio</p>';
  e.arquivos.slice().reverse().forEach(a=>{
   const d=document.createElement('div');d.className='lin';
   d.innerHTML=(a.nome=='_REDE.TXT'?'':'<input type="checkbox" class="sel" style="width:20px;height:20px">')+'<div class="n"></div><small>'+kb(a.tamanho)+' · '+h(a.data)+'</small><button class="v">apagar</button>';
   const cx=d.querySelector('.sel');
   if(cx){cx.checked=marcados.has(a.nome);cx.onchange=()=>{cx.checked?marcados.add(a.nome):marcados.delete(a.nome);barraSel()};d.querySelector('.n').onclick=()=>cx.click();d.querySelector('.n').style.cursor='pointer'}
   d.querySelector('.n').textContent=a.nome;
   d.querySelector('button').onclick=()=>apagar(a.nome);
   $('lista').appendChild(d)})}
 barraSel();
 const f=[['Situação','<b>'+s[1]+'</b>'],
  ['Endereço',e.ip?'<a href="http://'+h(e.ip)+'/">'+h(e.ip)+'</a>':'sem Wi-Fi'],
  ['Wi-Fi',e.ssid?h(e.ssid)+' · sinal '+(e.rssi>-60?'forte':e.rssi>-72?'médio':'fraco')+' ('+e.rssi+' dBm)':'não conectado'],
  ['Ligada há',dur(e.ligado)+' · já ligou '+e.boots+' vezes'],
  ['Entregas à máquina',e.entregasOk+' conferidas'+(e.entregasRuins?' · <b style="color:var(--er)">'+e.entregasRuins+' com erro</b>':' · nenhuma com erro')],
  ['Última leitura',e.ultLeitura<0?'nenhuma desde que ligou':'há '+dur(e.ultLeitura)],
  ['Porta recuperada sozinha',e.socorros+(e.socorros==1?' vez':' vezes')+(e.socorros?' · última há '+dur(e.ultSocorro):'')],
  ['Quedas de Wi-Fi',e.quedasWifi+(e.motivoQueda?' ('+h(e.motivoQueda)+')':'')],
  ['Espaço',kb(e.livre)+' livres de '+kb(e.total)],
  ['Versão',h(e.versao)]];
 const t=f.map(x=>'<span>'+x[0]+'</span><div>'+x[1]+'</div>').join('')+'<span></span><div style="color:var(--cz)">Os números contam desde que a plaquinha ligou.</div>';
 if($('saude')._h!=t){$('saude').innerHTML=t;$('saude')._h=t}
}
function B(){return P[foco]?P[foco].base:''}
function barraSel(){
 const n=marcados.size;
 $('apsel').style.display=n?'':'none';$('apsel').textContent='Apagar selecionados ('+n+')';$('apsel').onclick=apagarSel;
 $('todos').checked=n>0&&n==nomes.length;
 $('todos').onchange=()=>{$('todos').checked?nomes.forEach(x=>marcados.add(x)):marcados.clear();document.querySelectorAll('.sel').forEach(c=>c.checked=$('todos').checked);barraSel()};
}
async function apagarSel(){
 const lote=[...marcados],b=B();$('apsel').disabled=true;let ok=0;
 for(const n of lote){msg('Apagando '+(ok+1)+' de '+lote.length+'…');try{const r=await fetch(b+'/api/apagar?nome='+encodeURIComponent(n),{method:'POST'});if(r.ok)ok++}catch(e){}}
 marcados.clear();$('apsel').disabled=false;msg(ok+' arquivo(s) apagado(s).',ok==lote.length?'var(--ok)':'var(--er)');tique();
}
async function apagarTudo(){
 const p=P[foco];if(!confirm('Apagar todos os arquivos da '+p.nome+'?'))return;
 $('limpa').disabled=true;msg('Apagando tudo…');
 try{await fetch(p.base+'/api/apagar-tudo',{method:'POST'});msg(p.nome+' vazia.','var(--ok)')}catch(e){msg('Não deu para apagar.','var(--er)')}
 $('limpa').disabled=false;tique();
}
async function apagar(n){
 if(!confirm('Apagar '+n+'?'))return;
 await fetch(B()+'/api/apagar?nome='+encodeURIComponent(n),{method:'POST'});tique();
}

// ---- envio: um arquivo por vez, só para a plaquinha escolhida; nunca apaga os anteriores ----
function envia1(p,f,rot){return new Promise(ok=>{
 const fd=new FormData();fd.append('arquivo',f,f.name);
 const x=new XMLHttpRequest();
 x.open('POST',p.base+'/enviar?substituir=0');
 msg('Enviando '+rot+f.name+'…');
 x.upload.onprogress=e=>{if(e.lengthComputable)$('barra').firstChild.style.width=(100*e.loaded/e.total)+'%'};
 x.onload=()=>{let r={};try{r=JSON.parse(x.responseText)}catch(e){}ok(x.status==200&&r.ok?'':(r.erro||'falhou'))};
 x.onerror=()=>ok('sem conexão com a plaquinha');
 x.send(fd);
})}
async function enviar(lista,ids){
 const fs=[...(lista||[])];if(!fs.length)return;
 if(enviando){msg('Espere o envio atual terminar.','var(--er)');return}
 const al=[ids?ids[0]:foco].map(i=>P[i]).filter(p=>p&&p.on);
 if(!al.length){msg('Nenhuma plaquinha no ar para receber.','var(--er)');return}
 enviando=true;$('barra').style.display='block';
 const erros=[];
 for(const p of al){p.ocupada=true;
  for(let i=0;i<fs.length;i++){
   $('barra').firstChild.style.width=0;
   const e=await envia1(p,fs[i],(fs.length>1?'('+(i+1)+'/'+fs.length+') ':''));
   if(e)erros.push(p.nome+' · '+fs[i].name+': '+e);
  }p.ocupada=false}
 enviando=false;$('barra').style.display='none';$('barra').firstChild.style.width=0;
 const onde=al.length>1?'em '+al.length+' plaquinhas':'na '+al[0].nome;
 if(!erros.length)msg('✓ '+(fs.length>1?fs.length+' arquivos já estão ':fs[0].name+' já está ')+onde,'var(--ok)');
 else msg('✗ '+erros.join(' · '),'var(--er)');
 tique();
}

// ---- configurações da escolhida ----
async function salvarCfg(){
 await fetch(B()+'/api/config?nome='+encodeURIComponent($('nome').value)+'&replugar='+$('modo').value,{method:'POST'});
 msg('Salvo. Se mudou o nome, a plaquinha reinicia.','var(--ok)');cfgDe=null;setTimeout(tique,4000);
}
async function salvarWifi(){
 const s=$('ssid').value||$('redes').value;if(!s)return;
 if(!confirm('A '+P[foco].nome+' vai reiniciar e entrar na rede '+s+'. Continuar?'))return;
 await fetch(B()+'/api/wifi?ssid='+encodeURIComponent(s)+'&senha='+encodeURIComponent($('senha').value),{method:'POST'});
 alert('Reiniciando para conectar em '+s+'. Depois acesse '+$('nome').value.toLowerCase()+'.local');
}
async function redes(){
 $('redes').innerHTML='<option value="">procurando redes…</option>';
 try{const r=await(await busca(B()+'/api/redes',null,15000)).json();
 $('redes').innerHTML=r.map(n=>'<option></option>').join('');
 [...$('redes').options].forEach((o,i)=>o.textContent=o.value=r[i]);}catch(e){$('redes').innerHTML='<option value="">não consegui procurar</option>'}
}
function gravacao(){if(confirm('A '+P[foco].nome+' sai do ar até ser gravada pelo cabo. Continuar?'))fetch(B()+'/api/modo-gravacao',{method:'POST'})}
async function atualizar(ids,campo){
 const f=campo.files[0];if(!f){alert('Escolha o arquivo .bin primeiro.');return}
 const al=ids.map(i=>P[i]).filter(Boolean).sort((a,b)=>(a.eu?1:0)-(b.eu?1:0));
 if(!confirm('Atualizar o programa de '+(al.length>1?al.length+' plaquinhas':'"'+al[0].nome+'"')+' com '+f.name+'?'))return;
 const log=t=>{$('fwlog').innerHTML+='<div>'+h(t)+'</div>';msg(t)};$('fwlog').innerHTML='';
 for(const p of al){
  if(!p.on){log(p.nome+': fora do ar, pulei.');continue}
  if(al.length>1&&p.e.ultLeitura>=0&&p.e.ultLeitura<60){log(p.nome+': a máquina está lendo, pulei.');continue}
  log(p.nome+': enviando, não desligue…');p.ocupada=true;
  try{const fd=new FormData();fd.append('fw',f);const t=await(await fetch(p.base+'/atualizar',{method:'POST',body:fd})).text();log(p.nome+': '+t)}
  catch(e){log(p.nome+': não consegui enviar.')}
  p.ocupada=false;
 }
 setTimeout(tique,8000);
}
$('esquece').onclick=()=>{const p=P[foco];if(!p||p.eu)return;if(!confirm('Tirar "'+p.nome+'" do painel? Se ela estiver ligada na rede, volta sozinha.'))return;delete P[foco];foco=eu;alvos.clear();guarda();pinta()};
$('btadd').onclick=()=>{$('addform').style.display='';$('addnome').focus()};
$('addnao').onclick=()=>{$('addform').style.display='none'};
$('addok').onclick=async()=>{const n=$('addnome').value.trim();if(!n)return;const r=await fetch('/api/adicionar?nome='+encodeURIComponent(n),{method:'POST'});if(r.ok){$('addform').style.display='none';$('addnome').value='';descobrir()}};
$('addcanc').onclick=async()=>{await fetch('/api/adicionar?cancelar=1',{method:'POST'});addAtivo=false;$('addfim').textContent='';descobrir()};
$('zona').onclick=()=>$('arq').click();
$('arq').onchange=e=>{enviar(e.target.files);e.target.value=''};
['dragenter','dragover'].forEach(v=>$('zona').addEventListener(v,e=>{e.preventDefault();$('zona').classList.add('sobre')}));
['dragleave','drop'].forEach(v=>$('zona').addEventListener(v,e=>{e.preventDefault();$('zona').classList.remove('sobre')}));
$('zona').addEventListener('drop',e=>enviar(e.dataTransfer.files));
window.addEventListener('dragover',e=>e.preventDefault());window.addEventListener('drop',e=>e.preventDefault());
carrega();pinta();
(async()=>{await descobrir();await tique();setInterval(tique,4000);setInterval(descobrir,3000)})();
</script></body></html>)HTML";
