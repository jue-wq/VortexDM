const browser = globalThis.browser || globalThis.chrome;
const $=id=>document.getElementById(id);
async function refresh(){
  const state=await browser.runtime.sendMessage({type:"status"});
  $("connection").textContent=state.accepted===false?"VortexDM bağlantısı yok":"VortexDM bağlantısı hazır";
  const queues=state.queues||[];
  if(queues.length){$("queue").replaceChildren(...queues.map(q=>{const o=document.createElement("option");o.value=q.id;o.textContent=q.name;return o;}));}
  const tasks=state.tasks||state.downloads||[];
  $("tasks").replaceChildren();
  if(!tasks.length){$("tasks").textContent="Henüz görev yok";return;}
  for(const task of tasks){const el=document.createElement("div");el.className="task";const name=document.createElement("div");name.textContent=task.filename||task.id;const detail=document.createElement("div");detail.className="muted";detail.textContent=`${task.status||"Unknown"} · ${task.downloaded_bytes||0}/${task.total_bytes||0}`;el.append(name,detail);$("tasks").append(el);}
}
$("refresh").addEventListener("click",refresh);
$("add").addEventListener("click",async()=>{const [tab]=await browser.tabs.query({active:true,currentWindow:true});if(!tab?.url)return;const r=await browser.runtime.sendMessage({type:"addTab",url:tab.url,filename:tab.title,queue_id:$("queue").value});$("connection").textContent=r.accepted?"Görev eklendi":(r.message||r.error||"Görev eklenemedi");});
refresh();
