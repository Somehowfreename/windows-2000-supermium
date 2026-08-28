const fs = await import('node:fs');
const base = process.argv[2] || 'http://127.0.0.1:19226';
const output = process.argv[3];
const delay = ms => new Promise(r=>setTimeout(r,ms));
const targets = await (await fetch(base+'/json/list')).json();
const target = targets.find(x=>x.type==='page');
if(!target) throw new Error('No page target');
const ws = new WebSocket(target.webSocketDebuggerUrl);
const pending = new Map(); let id=1;
function command(method,params={}){const n=id++;return new Promise((resolve,reject)=>{const timer=setTimeout(()=>{pending.delete(n);reject(new Error(method+' timeout'));},30000);pending.set(n,{resolve,reject,timer});ws.send(JSON.stringify({id:n,method,params}));});}
ws.onmessage=e=>{const m=JSON.parse(e.data);if(!m.id||!pending.has(m.id))return;const p=pending.get(m.id);pending.delete(m.id);clearTimeout(p.timer);m.error?p.reject(new Error(JSON.stringify(m.error))):p.resolve(m.result);};
await new Promise((resolve,reject)=>{ws.onopen=resolve;ws.onerror=reject;});
async function evalv(expression,userGesture=false){const r=await command('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true,userGesture});return r.result?.value;}
const stateExpr="(()=>{const v=document.querySelector('video');if(!v)return null;const q=v.getVideoPlaybackQuality?.();return{currentTime:v.currentTime,duration:v.duration,paused:v.paused,seeking:v.seeking,readyState:v.readyState,volume:v.volume,muted:v.muted,error:v.error&&{code:v.error.code,message:v.error.message},decodedFrames:q?.totalVideoFrames??v.webkitDecodedFrameCount,droppedFrames:q?.droppedVideoFrames??v.webkitDroppedFrameCount,audioDecodedBytes:v.webkitAudioDecodedByteCount||0};})()";
const report={capturedAt:new Date().toISOString(),base,targetId:target.id};
try{
 report.initial=await evalv(stateExpr);
 await evalv("(()=>{const v=document.querySelector('video');v.muted=true;v.pause();return true;})()",true); await delay(2500); report.afterPause=await evalv(stateExpr);
 report.resumeResult=await evalv("(async()=>{const v=document.querySelector('video');try{await v.play();return 'playing';}catch(e){return e.name+':'+e.message;}})()",true); await delay(4000); report.afterResume=await evalv(stateExpr);
 report.volumeSet=await evalv("(()=>{const v=document.querySelector('video');v.volume=0.25;v.muted=false;return{volume:v.volume,muted:v.muted};})()",true);
 await evalv("(()=>{const v=document.querySelector('video');v.muted=true;return true;})()");
 const beforeSeek=await evalv(stateExpr); const seekTarget=Math.min(beforeSeek.currentTime+10,Math.max(0,beforeSeek.duration-2));
 await evalv("(()=>{const v=document.querySelector('video');v.currentTime="+seekTarget+";return v.currentTime;})()",true); await delay(5000); report.seek={requested:seekTarget,before:beforeSeek,after:await evalv(stateExpr)};
 const made=await command('Target.createTarget',{url:'about:blank'}); report.tab={createdTargetId:made.targetId}; await delay(1000); report.tab.closeResult=await command('Target.closeTarget',{targetId:made.targetId}); const afterTargets=await command('Target.getTargets'); report.tab.presentAfterClose=afterTargets.targetInfos.some(x=>x.targetId===made.targetId);
 report.assessment={pauseWorked:report.afterPause?.paused===true&&Math.abs(report.afterPause.currentTime-report.initial.currentTime)<1,resumeWorked:report.afterResume?.paused===false&&report.afterResume.currentTime>report.afterPause.currentTime+1,volumeWorked:report.volumeSet?.volume===0.25&&report.volumeSet?.muted===false,seekWorked:Math.abs(report.seek.after.currentTime-report.seek.requested)<8,tabCloseWorked:report.tab.closeResult?.success===true&&!report.tab.presentAfterClose,mediaErrorObserved:Boolean(report.afterPause?.error||report.afterResume?.error||report.seek.after?.error)};
}catch(error){report.fatalError=error.stack||String(error);}finally{report.completedAt=new Date().toISOString();if(output)fs.writeFileSync(output,JSON.stringify(report,null,2)+'\n');console.log(JSON.stringify(report,null,2));ws.close();}
