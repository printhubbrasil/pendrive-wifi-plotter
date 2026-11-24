#pragma once
const char PAGINA[] PROGMEM = R"HTML(<!doctype html>
<html lang="pt-BR"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Pen Drive de Rede</title>
<style>
:root{--az:#00a8e8;--fu:#f4f6f8;--tx:#1d2733;--cz:#6b7785;--ok:#1aa260;--er:#d93838}
*{box-sizing:border-box}body{margin:0;font:16px system-ui,sans-serif;background:var(--fu);color:var(--tx)}
main{max-width:640px;margin:0 auto;padding:16px}
h1{font-size:22px;margin:4px 0 2px}.sub{color:var(--cz);font-size:14px;margin-bottom:16px}
.card{background:#fff;border-radius:12px;padding:16px;margin-bottom:14px;box-shadow:0 1px 3px #0001}
#zona{border:2px dashed var(--az);border-radius:12px;padding:34px 12px;text-align:center;cursor:pointer;color:var(--az);font-weight:600}
#zona.sobre{background:#e6f6fd}
.barra{height:8px;background:#e3e8ee;border-radius:4px;overflow:hidden;margin-top:12px;display:none}
.barra i{display:block;height:100%;width:0;background:var(--az)}
#msg{margin-top:10px;font-weight:600;min-height:20px}
.lin{display:flex;align-items:center;gap:8px;padding:10px 0;border-bottom:1px solid #eef1f4}
.lin:last-child{border:0}.lin .n{flex:1;word-break:break-all}.lin small{color:var(--cz)}
button{font:inherit;border:0;border-radius:8px;padding:8px 12px;background:#e3e8ee;cursor:pointer}
button.v{background:#fde8e8;color:var(--er)}
label{display:flex;gap:8px;align-items:center;margin:8px 0}
input[type=text],input[type=password],select{font:inherit;width:100%;padding:8px;border:1px solid #cfd6de;border-radius:8px;margin:4px 0 10px}
details summary{cursor:pointer;font-weight:600}
</style></head><body><main>
<h1 id="tit">Pen Drive de Rede</h1>
<div class="sub" id="sub">carregando…</div>

<div class="card">
<div id="zona">Arraste os arquivos aqui<br><small style="font-weight:400">ou toque para escolher (pode ser vários)</small></div>
<input type="file" id="arq" multiple hidden>
<div class="barra" id="barra"><i></i></div>
<div id="msg"></div>
<label><input type="checkbox" id="subst"> Apagar os arquivos anteriores ao enviar</label>
</div>

<div class="card"><div style="display:flex;align-items:center;gap:8px"><b style="flex:1">No pen drive agora <span id="qtd" style="color:var(--cz);font-weight:400"></span></b><button class="v" id="limpa" style="font-weight:600">Apagar tudo</button></div><div id="selbar" style="display:flex;align-items:center;gap:8px;margin-top:10px"><label style="flex:1;margin:0"><input type="checkbox" id="todos"> Selecionar todos</label><button class="v" id="apsel" style="display:none"></button></div><div id="lista"></div></div>

<div class="card"><div style="display:flex;align-items:center;gap:8px"><b style="flex:1">Plaquinhas na rede</b><button id="btadd">Adicionar plaquinha</button></div>
<div id="placas"></div>
<div id="addform" style="display:none;margin-top:10px"><p style="margin:0 0 4px">Nome da plaquinha nova (ex.: teneth, saga2):</p><input type="text" id="addnome" maxlength="11"><button id="addok">Começar</button> <button id="addnao">Cancelar</button></div>
<div id="addprog" style="display:none;margin-top:10px;padding:10px;border-radius:8px;background:#e6f6fd"><b id="addtit"></b><div id="addtxt" style="margin:6px 0"></div><button id="addcanc">Cancelar</button></div>
<p id="addfim" style="font-weight:600;margin:10px 0 0"></p>
</div>

<div class="card"><details><summary>Configurações</summary>
<p><b>Nome da plaquinha</b> (vira o endereço <i>nome</i>.local e o nome do pen drive)</p>
<input type="text" id="nome" maxlength="11">
<p><b>Ao trocar o arquivo</b></p>
<select id="modo"><option value="1">Desplugar e plugar o USB (mais garantido)</option>
<option value="0">Só trocar o disco (mais rápido)</option></select>
<button onclick="salvarCfg()">Salvar</button>
<hr style="border:0;border-top:1px solid #eef1f4;margin:16px 0">
<p><b>Wi-Fi</b></p>
<select id="redes"><option>procurando redes…</option></select>
<input type="text" id="ssid" placeholder="ou digite o nome da rede">
<input type="password" id="senha" placeholder="senha">
<button onclick="salvarWifi()">Conectar</button>
<hr style="border:0;border-top:1px solid #eef1f4;margin:16px 0">
<p><b>Atualizar programa</b> (arquivo .bin)</p>
<input type="file" id="fw" accept=".bin"><button onclick="atualizar()">Atualizar</button>
<button onclick="if(confirm('A plaquinha sai do ar até ser gravada pelo cabo. Continuar?'))fetch('/api/modo-gravacao',{method:'POST'})">Modo gravação pelo cabo</button>
<p id="info" style="color:var(--cz);font-size:13px"></p>
</details></div>
</main><script>
const $=i=>document.getElementById(i);
function kb(b){return b>1048576?(b/1048576).toFixed(1)+' MB':Math.ceil(b/1024)+' KB'}
function msg(t,cor){$('msg').textContent=t;$('msg').style.color=cor||''}
function dur(s){return s<60?s+' s':s<3600?Math.floor(s/60)+' min':Math.floor(s/3600)+' h '+Math.floor(s%3600/60)+' min'}
async function estado(){
 const e=await (await fetch('/api/estado')).json();
 $('tit').textContent='Pen Drive · '+e.nome;
 $('sub').textContent=kb(e.livre)+' livres de '+kb(e.total)+(e.ip?' · '+e.ip:'');
 if(e.pend){$('sub').textContent+=' · atualizando o pen drive…';setTimeout(estado,1500)}
 $('subst').checked=e.substituir;
 if(document.activeElement!==$('nome'))$('nome').value=e.nome;
 $('modo').value=e.replugar?'1':'0';
 $('info').textContent='versão '+e.versao+' · Wi-Fi: '+(e.ssid||'não conectado')+(e.rssi?' ('+e.rssi+' dBm)':'')+(e.boots?' · ligado há '+dur(e.ligado)+' · ligou '+e.boots+'x ('+e.motivoBoot+') · quedas de Wi-Fi: '+e.quedasWifi+(e.motivoQueda?' ['+e.motivoQueda+']':'')+' · saídas do USB: '+e.replugs+' · última leitura da máquina: '+(e.ultLeitura<0?'nenhuma':'há '+dur(e.ultLeitura)):'');
 nomes=e.arquivos.map(a=>a.nome).filter(n=>n!='_REDE.TXT');marcados.forEach(n=>{if(!nomes.includes(n))marcados.delete(n)});
 const nArq=nomes.length;$('selbar').style.display=nArq?'flex':'none';$('qtd').textContent='('+nArq+')';$('limpa').onclick=apagarTudo;$('limpa').style.display=nArq?'':'none';
 $('lista').innerHTML=e.arquivos.length?'':'<p style="color:var(--cz)">vazio</p>';
 e.arquivos.slice().reverse().forEach(a=>{
  const d=document.createElement('div');d.className='lin';
  d.innerHTML=(a.nome=='_REDE.TXT'?'':'<input type="checkbox" class="sel" style="width:20px;height:20px">')+'<div class="n"></div><small>'+kb(a.tamanho)+' · '+a.data+'</small><button class="v">apagar</button>';
  const cx=d.querySelector('.sel');
  if(cx){cx.checked=marcados.has(a.nome);cx.onchange=()=>{cx.checked?marcados.add(a.nome):marcados.delete(a.nome);barraSel()};d.querySelector('.n').onclick=()=>cx.click();d.querySelector('.n').style.cursor='pointer'}
  d.querySelector('.n').textContent=a.nome;
  d.querySelector('button').onclick=()=>apagar(a.nome);
  $('lista').appendChild(d);
 });
 barraSel();
}
let marcados=new Set(),nomes=[];
function barraSel(){
 const n=marcados.size;
 $('apsel').style.display=n?'':'none';$('apsel').textContent='Apagar selecionados ('+n+')';$('apsel').onclick=apagarSel;
 $('todos').checked=n>0&&n==nomes.length;
 $('todos').onchange=()=>{$('todos').checked?nomes.forEach(x=>marcados.add(x)):marcados.clear();document.querySelectorAll('.sel').forEach(c=>c.checked=$('todos').checked);barraSel()};
}
async function apagarSel(){
 const lote=[...marcados];$('apsel').disabled=true;let ok=0;
 for(const n of lote){msg('Apagando '+(ok+1)+' de '+lote.length+'…');try{const r=await fetch('/api/apagar?nome='+encodeURIComponent(n),{method:'POST'});if(r.ok)ok++}catch(e){}}
 marcados.clear();$('apsel').disabled=false;msg(ok+' arquivo(s) apagado(s).',ok==lote.length?'var(--ok)':'var(--er)');estado();
}
// um por vez, em fila; "apagar anteriores" vale só pro 1º, senão o lote apaga a si mesmo
function envia1(f,subst,rot){return new Promise(ok=>{
 const fd=new FormData();fd.append('arquivo',f,f.name);
 const x=new XMLHttpRequest();
 x.open('POST','/enviar?substituir='+(subst?1:0));
 msg('Enviando '+rot+f.name+'…');
 x.upload.onprogress=e=>{if(e.lengthComputable)$('barra').firstChild.style.width=(100*e.loaded/e.total)+'%'};
 x.onload=()=>{let r={};try{r=JSON.parse(x.responseText)}catch(e){}ok(x.status==200&&r.ok?'':(r.erro||'falhou'))};
 x.onerror=()=>ok('sem conexão com a plaquinha');
 x.send(fd);
})}
async function enviar(lista){
 const fs=[...(lista||[])];if(!fs.length)return;
 $('barra').style.display='block';
 const erros=[];
 for(let i=0;i<fs.length;i++){
  $('barra').firstChild.style.width=0;
  const e=await envia1(fs[i],i==0&&$('subst').checked,fs.length>1?'('+(i+1)+'/'+fs.length+') ':'');
  if(e)erros.push(fs[i].name+': '+e);
 }
 $('barra').style.display='none';$('barra').firstChild.style.width=0;
 if(!erros.length)msg('✓ '+(fs.length>1?fs.length+' arquivos já estão':fs[0].name+' já está')+' no pen drive','var(--ok)');
 else msg('✗ '+erros.join(' · '),'var(--er)');
 estado();
}
async function apagarTudo(){
 $('limpa').disabled=true;msg('Apagando tudo…');
 try{await fetch('/api/apagar-tudo',{method:'POST'});msg('Pen drive vazio.','var(--ok)')}catch(e){msg('Não deu para apagar.','var(--er)')}
 $('limpa').disabled=false;estado();
}
let addAtivo=false;
async function placas(){
 let p;try{p=await (await fetch('/api/placas')).json()}catch(e){return}
 const L=$('placas');L.innerHTML='';
 const lin=(nome,det,url)=>{const d=document.createElement('div');d.className='lin';d.innerHTML='<div class="n"></div><small></small>';const n=d.querySelector('.n');if(url){const a=document.createElement('a');a.href=url;a.textContent=nome;a.style.color='var(--az)';a.style.fontWeight='600';n.appendChild(a)}else{n.textContent=nome;n.style.fontWeight='600'}d.querySelector('small').textContent=det;L.appendChild(d)};
 lin(p.eu.nome+' (esta)',p.eu.ip+' · versão '+p.eu.versao);
 p.placas.sort((a,b)=>a.nome.localeCompare(b.nome)).forEach(x=>lin(x.nome,x.ip+' · versão '+x.versao+' · '+x.arquivos+' arquivo(s)','http://'+x.ip+'/'));
 const a=p.adicionar;
 if(a.resta>0){addAtivo=true;$('addprog').style.display='';$('addtit').textContent='Adicionando a plaquinha "'+a.nome+'"';$('addtxt').textContent=a.progresso;$('addfim').textContent='';$('btadd').style.display='none'}
 else{$('addprog').style.display='none';$('btadd').style.display='';if(addAtivo){addAtivo=false;$('addfim').textContent=a.progresso}}
}
$('btadd').onclick=()=>{$('addform').style.display='';$('addnome').focus()};
$('addnao').onclick=()=>{$('addform').style.display='none'};
$('addok').onclick=async()=>{const n=$('addnome').value.trim();if(!n)return;const r=await fetch('/api/adicionar?nome='+encodeURIComponent(n),{method:'POST'});if(r.ok){$('addform').style.display='none';$('addnome').value='';placas()}};
$('addcanc').onclick=async()=>{await fetch('/api/adicionar?cancelar=1',{method:'POST'});addAtivo=false;$('addfim').textContent='';placas()};
async function apagar(n){
 if(!confirm('Apagar '+n+'?'))return;
 await fetch('/api/apagar?nome='+encodeURIComponent(n),{method:'POST'});estado();
}
async function salvarCfg(){
 await fetch('/api/config?nome='+encodeURIComponent($('nome').value)+'&replugar='+$('modo').value+'&substituir='+($('subst').checked?1:0),{method:'POST'});
 msg('Salvo. Se mudou o nome, a plaquinha reinicia.','var(--ok)');setTimeout(estado,4000);
}
async function salvarWifi(){
 const s=$('ssid').value||$('redes').value;
 await fetch('/api/wifi?ssid='+encodeURIComponent(s)+'&senha='+encodeURIComponent($('senha').value),{method:'POST'});
 alert('Reiniciando para conectar em '+s+'. Depois acesse '+$('nome').value.toLowerCase()+'.local');
}
async function redes(){
 try{const r=await (await fetch('/api/redes')).json();
 $('redes').innerHTML=r.map(n=>'<option></option>').join('');
 [...$('redes').options].forEach((o,i)=>o.textContent=o.value=r[i]);}catch(e){}
}
function atualizar(){
 const f=$('fw').files[0];if(!f)return;
 const fd=new FormData();fd.append('fw',f);
 msg('Atualizando, não desligue…');
 fetch('/atualizar',{method:'POST',body:fd}).then(r=>r.text()).then(t=>msg(t,'var(--ok)'));
}
$('zona').onclick=()=>$('arq').click();
$('arq').onchange=e=>{enviar(e.target.files);e.target.value=''};
['dragenter','dragover'].forEach(v=>$('zona').addEventListener(v,e=>{e.preventDefault();$('zona').classList.add('sobre')}));
['dragleave','drop'].forEach(v=>$('zona').addEventListener(v,e=>{e.preventDefault();$('zona').classList.remove('sobre')}));
$('zona').addEventListener('drop',e=>enviar(e.dataTransfer.files));
$('subst').onchange=()=>fetch('/api/config?substituir='+($('subst').checked?1:0),{method:'POST'});
estado();redes();setInterval(estado,5000);placas();setInterval(placas,3000);
</script></body></html>)HTML";
