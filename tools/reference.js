/* Renders the factory patterns in the browser prototype with a fixed random source, so the C port can be compared sample for sample.
   Usage: node tools/reference.js path/to/grus.html seconds > writes data/ref_<k>.f64 (interleaved stereo, float64) */
const {chromium}=require('playwright'),fs=require('fs'),path=require('path');
(async()=>{const b=await chromium.launch(),p=await b.newPage();
  await p.goto('file://'+path.resolve(process.argv[2]));await p.waitForTimeout(500);
  const secs=+(process.argv[3]||3);
  for(let k=0;k<8;k++){
    const b64=await p.evaluate(([k,secs])=>{BOOT.on=false;chainSet([]);SONG.on=false;pendSlot=-1;switchTo(k);
      /* steps as the C core stores them: 16-bit values, ratchet curve and nudge in hundredths */
      const q=v=>Math.round(v*65535)/65535;
      TR.forEach(t=>t.steps.forEach(s=>{if(s.vel)s.vel=q(s.vel);for(const id in s.locks)s.locks[id]=q(s.locks[id]);
        if(s.slope!=null)s.slope=Math.round(s.slope*100)/100;if(s.nudge!=null)s.nudge=Math.round(s.nudge*100)/100}));
      /* the same xorshift as grus_random's default in C */
      let x=0x9E3779B9|0;const R=Math.random;Math.random=()=>{x^=x<<13;x^=x>>>17;x^=x<<5;return(x>>>0)/4294967296};
      SR=44100;mkRV();mkDL();PB=null;LIM=null;pumpEnv=0;lfsr=1;SET.lim=1;SET.ceil=-1;SET.spk=0;fillOn=false;
      Object.assign(PF,{mode:null,lp:0,hp:0,lpx:0,hpx:0,frz:0,dub:0,frzE:0,dubE:0});
      TR.forEach(t=>{Object.assign(t,{v:{},S:null,ef:0,kf:false,kg:1,lph:0,lv:0,sh:0,pk:0,fs:[0,0],fss:[0,0],cl:null,hits:0,side:0,pos:-1,sign:1,loop:0,left:0,ev:[]})});
      playing=true;gLeft=0;gStep=-1;cycPos=0;
      const N=Math.round(secs*SR/1024)*1024,out=new Float64Array(N*2),L=new Float64Array(1024),Rr=new Float64Array(1024);
      for(let s=0;s<N;s+=1024){spb=SR*60/sl.bpm/4;render(L,Rr,1024);for(let i=0;i<1024;i++){out[(s+i)*2]=L[i];out[(s+i)*2+1]=Rr[i]}}
      playing=false;Math.random=R;
      const u=new Uint8Array(out.buffer);let s='';for(let i=0;i<u.length;i+=0x8000)s+=String.fromCharCode.apply(null,u.subarray(i,i+0x8000));return btoa(s)},[k,secs]);
    fs.writeFileSync('data/ref_'+k+'.f64',Buffer.from(b64,'base64'));process.stderr.write('ref '+k+'\n')}
  await b.close()})();
