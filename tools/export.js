/* Pulls everything the C core needs out of the browser prototype: parameter tables, the factory patterns and the factory samples as flash bytes.
   Usage: node tools/export.js path/to/grus.html > data/export.json */
const {chromium}=require('playwright');
(async()=>{const b=await chromium.launch(),p=await b.newPage();
  await p.goto('file://'+require('path').resolve(process.argv[2]));await p.waitForTimeout(500);
  const d=await p.evaluate(()=>{BOOT.on=false;
    const EORD=['fm','modal','puls','pad','lead','stab','smp'];
    const MIXI=['lvl','pan','send','dly'],MODI=['filt','fres','lform','lrate','ldep','ltgt','lrst','pump'];
    const eng=EORD.map(e=>({id:e,name:ENG[e].name,params:ENG[e].params.map(p=>({id:p.id,n:p.n||0})),base:ENG[e].params.map(p=>BASE[e][p.id])}));
    const mod=modP('fm').map(p=>({id:p.id,n:p.n||0}));
    const lockIndex=(e,id)=>{const i=ENG[e].params.findIndex(p=>p.id===id);if(i>=0)return i;const m=MIXI.indexOf(id);if(m>=0)return 8+m;const o=MODI.indexOf(id);return o>=0?16+o:-1};
    const pats=[];
    FACTORY.forEach((f,k)=>{switchTo(k);const S={...sl};
      pats.push({name:f.name,sl:{bpm:S.bpm,swing:S.swing,vol:S.vol,rsize:S.rsize,rdamp:S.rdamp,dtype:S.dtype,dtime:S.dtime,dfb:S.dfb,dtone:S.dtone,dmod:S.dmod,dret:S.dret,voices:S.voices||8},
        tracks:TR.map(t=>({eng:EORD.indexOf(t.eng),pv:ENG[t.eng].params.map(p=>t.pv[t.eng][p.id]),mx:MIXI.map(i=>t.mx[i]),mo:MODI.map(i=>t.mo[i]),len:t.len,div:t.div,dir:t.dir,mute:t.mute?1:0,
          steps:t.steps.map((s,i)=>({i,vel:s.vel,note:s.note??0,prob:s.prob??100,rat:s.rat??1,slope:s.slope??0,nudge:s.nudge??0,mod:s.mod??0,
            locks:Object.entries(s.locks||{}).map(([id,v])=>[lockIndex(t.eng,id),v]).filter(([i])=>i>=0),ci:s.ci||null})).filter(s=>s.vel>0||s.locks.length)}))})});
    const smp=SMP.map(m=>({name:m.name,n:m.n,sl:m.sl,a:Array.from(m.a)}));
    const pres=EORD.map(en=>(SPRE[en]||[]).map(([name,v])=>({name,v:ENG[en].params.map(p=>v[p.id]??null)})));
    return{eng,mod,pats,smp,pres,ratios:RATIOS,chn:CHI,stc:STC,lrate:LRATE.map(x=>x[0]),divs:DIVS.map(x=>x[0]),mods:MODS.map(m=>m==='–'?[0,0]:m.split(':').map(Number)),smpSR:SMP_SR}});
  process.stdout.write(JSON.stringify(d));await b.close()})();
