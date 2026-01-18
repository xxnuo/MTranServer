import {afterEach,expect,spyOn,test,mock} from 'bun:test';
import * as backend from '../src/core/backend';
import * as native from '../src/core/native';
import {TranslationEngine} from '../src/core/engine';
import {Tokenizer} from '../src/core/tokenizer';
import {Shortlist} from '../src/core/shortlist';
import * as ort from 'onnxruntime-node';
const tuning={executionMode:'sequential',interOpNumThreads:1,graphOptimizationLevel:'all',enableCpuMemArena:true,enableMemPattern:true,spinning:true,dynamicBlockBase:0} as const;
afterEach(()=>mock.restore());
function setup(fails=false,shortlist=true){
 const original={inputNames:shortlist?['X','eos_id','candidate_ids']:['X','eos_id'],release:mock(async()=>{}),run:mock(async()=>({Y:new ort.Tensor('int64',BigInt64Array.of(7n,0n),[2])}))};
 const parallel={...original,release:mock(async()=>{}),run:mock(async()=>{if(fails)throw new Error('native probe failed');return {Y:new ort.Tensor('int64',BigInt64Array.of(7n,0n),[2])};})};
 spyOn(Tokenizer,'load').mockResolvedValue({encode:()=>[2],decode:()=> 'result',eos:0,close(){}} as any);
 spyOn(Shortlist,'load').mockResolvedValue({candidates:()=>[0,2,7]} as any);
 spyOn(backend,'selectBackend').mockResolvedValue({session:original,info:{provider:'cpu',cpuThreads:2,tuning,maxConcurrentRuns:1,measurements:[]},fallbackCpuThreads:2,fallbackCpuTuning:tuning} as any);
 spyOn(native,'createNativeSession').mockResolvedValue(parallel as any);
 return {original,parallel};
}
const model={directory:'.',identity:'native-test',model:'fake.onnx',sourceVocab:'src',targetVocab:'dst',lex:'lex'};
test('standard CPU uses shared execution without loading a shortlist',async()=>{
 const {original,parallel}=setup(false,false);const engine=new TranslationEngine();
 try{await engine.init({...model,lex:undefined});
 expect(engine.backend?.runtime).toBe('native-async');
 expect(native.createNativeSession).toHaveBeenCalledWith(model.model,['X','eos_id'],2);
 expect(engine.backend?.cpuThreads).toBe(2);
 expect(Shortlist.load).not.toHaveBeenCalled();
 expect(await Promise.all(Array.from({length:8},()=>engine.translateAsync('hello')))).toEqual(Array(8).fill('result'));
 expect(original.release).toHaveBeenCalledTimes(1);
 for(const [feeds] of parallel.run.mock.calls as any[])expect(feeds.candidate_ids).toBeUndefined();
 }finally{await engine.destroy();}
});
test('fast CPU uses the validated shared session and closes it after accepted work',async()=>{
 const {original,parallel}=setup();const engine=new TranslationEngine();
 try{await engine.init(model);expect(engine.backend?.runtime).toBe('native-async');expect(engine.backend?.maxConcurrentRuns).toBeGreaterThanOrEqual(1);expect(original.release).toHaveBeenCalledTimes(1);
 expect(await engine.translateAsync('hello')).toBe('result');}finally{await engine.destroy();}
 expect(parallel.release).toHaveBeenCalledTimes(1);
});
test('native validation failure retains and serves the standard session',async()=>{
 const {original,parallel}=setup(true);const engine=new TranslationEngine();
 try{await engine.init(model);expect(original.release).not.toHaveBeenCalled();expect(parallel.release).toHaveBeenCalledTimes(1);expect(await engine.translateAsync('hello')).toBe('result');}finally{await engine.destroy();}
 expect(original.release).toHaveBeenCalledTimes(1);
});

test('concurrent native failures create one fallback and drain queued work on destroy',async()=>{
 const {original,parallel}=setup();const engine=new TranslationEngine();
 await engine.init(model);
 parallel.run.mockImplementation(async()=>{throw new native.NativeRunError('native failed');});
 const initial = engine.backend!.maxConcurrentRuns;
 let active=0,calls=0;
 const fallback={...original,release:mock(async()=>{}),run:async()=>{
  active++; calls++;
  if(calls>initial)expect(active).toBe(1);
  await new Promise(r=>setTimeout(r,1));active--;
  return {Y:new ort.Tensor('int64',BigInt64Array.of(7n,0n),[2])};
 }};
 const create=spyOn(backend,'createSession').mockResolvedValue(fallback as any);
 const tasks=Array.from({length:9},()=>engine.translateAsync('hello'));
 const closing=engine.destroy();
 await expect(engine.translateAsync('late')).rejects.toThrow('closing');
 expect(await Promise.all(tasks)).toEqual(Array(9).fill('result'));
 await closing;
 expect(create).toHaveBeenCalledTimes(1);
 expect(parallel.release).toHaveBeenCalledTimes(1);
 expect(fallback.release).toHaveBeenCalledTimes(1);
 expect(engine.backend?.runtime).toBeUndefined();
 expect(engine.backend?.maxConcurrentRuns).toBe(1);
});

test('destroy waits for native initialization and closes the published session once',async()=>{
 const {original,parallel}=setup();let resume!:()=>void,notify!:()=>void;
 const gate=new Promise<void>(r=>resume=r),entered=new Promise<void>(r=>notify=r);
 spyOn(native,'createNativeSession').mockImplementation(async()=>{notify();await gate;return parallel as any;});
 const engine=new TranslationEngine();const init=engine.init(model);await entered;
 const close=engine.destroy();await Promise.resolve();
 expect(original.release).not.toHaveBeenCalled();resume();await init;await close;
 expect(parallel.release).toHaveBeenCalledTimes(1);
 await expect(engine.translateAsync('late')).rejects.toThrow('closing');
});

test('explicit concurrency one overrides automatic native parallelism',async()=>{
 setup();const engine=new TranslationEngine();
 try{await engine.init(model,{'max-concurrent-runs':1});expect(engine.backend?.maxConcurrentRuns).toBe(1);}
 finally{await engine.destroy();}
});

test.each([
 {input:'<p>one</p><b>two</b><i>three</i>',html:true,output:'<p>result</p><b>result</b><i>result</i>',count:4},
 {input:'First sentence. Second sentence. Third sentence.',html:false,output:'result result result',count:1},
 {input:'First sentence. Second sentence. Third sentence.',html:false,output:'result result result',count:4},
])('parts drain through one serialized fallback while the engine closes: %j',async({input,html,output,count})=>{
 const {original,parallel}=setup();const engine=new TranslationEngine();
 await engine.init(model,{'max-concurrent-runs':2,'max-length-break':20});
 parallel.run.mockImplementation(async()=>{throw new native.NativeRunError('HTML native failed');});
 let active=0,peak=0,calls=0;
 const fallback={...original,release:mock(async()=>{expect(active).toBe(0);}),run:async()=>{
  active++;calls++;peak=Math.max(peak,active);
  try{await new Promise(resolve=>setTimeout(resolve,1));return {Y:new ort.Tensor('int64',BigInt64Array.of(7n,0n),[2])};}
  finally{active--;}
 }};
 const create=spyOn(backend,'createSession').mockResolvedValue(fallback as any);
 const requests=Promise.all(Array.from({length:count},()=>engine.translateAsync(input,{html})));
 const closing=engine.destroy();
 expect(await requests).toEqual(Array(count).fill(output));
 await closing;expect(create).toHaveBeenCalledTimes(1);expect(calls).toBe(count*3);expect(peak).toBe(1);
 expect(parallel.release).toHaveBeenCalledTimes(1);expect(fallback.release).toHaveBeenCalledTimes(1);
 expect(engine.busy).toBe(false);
});
