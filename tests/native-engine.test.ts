import {afterEach,expect,spyOn,test,mock} from 'bun:test';
import * as backend from '../src/core/backend';
import * as native from '../src/core/native';
import {TranslationEngine} from '../src/core/engine';
import {Tokenizer} from '../src/core/tokenizer';
import {Shortlist} from '../src/core/shortlist';
import * as ort from 'onnxruntime-node';
const tuning={executionMode:'sequential',interOpNumThreads:1,graphOptimizationLevel:'all',enableCpuMemArena:true,enableMemPattern:true,spinning:true,dynamicBlockBase:0} as const;
afterEach(()=>mock.restore());
function setup(fails=false){
 const original={inputNames:['X','eos_id','candidate_ids'],release:mock(async()=>{}),run:mock(async()=>({Y:new ort.Tensor('int64',BigInt64Array.of(7n,0n),[2])}))};
 const parallel={...original,release:mock(async()=>{}),run:mock(async()=>{if(fails)throw new Error('native probe failed');return {Y:new ort.Tensor('int64',BigInt64Array.of(7n,0n),[2])};})};
 spyOn(Tokenizer,'load').mockResolvedValue({encode:()=>[2],decode:()=> 'result',eos:0,close(){}} as any);
 spyOn(Shortlist,'load').mockResolvedValue({candidates:()=>[0,2,7]} as any);
 spyOn(backend,'selectBackend').mockResolvedValue({session:original,info:{provider:'cpu',cpuThreads:2,tuning,maxConcurrentRuns:1,measurements:[]},fallbackCpuThreads:2,fallbackCpuTuning:tuning} as any);
 spyOn(native,'createNativeSession').mockResolvedValue(parallel as any);
 return {original,parallel};
}
const model={directory:'.',identity:'native-test',model:'fake.onnx',sourceVocab:'src',targetVocab:'dst',lex:'lex'};
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
