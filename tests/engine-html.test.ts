import { describe, expect, test } from 'bun:test';
import { TranslationEngine } from '../src/core/engine';

describe('ONNX text handling', () => {
  test('HTML preserves markup, attributes, protected content and escapes translated text', async () => {
    const engine = new TranslationEngine() as any;
    engine.translateText = async (s: string) => s.replace('Hallo', 'Hello & <world>');
    const html = '<p title="Hallo">Hallo <b translate="no">Hallo</b><script>Hallo</script></p>';
    expect(await engine.translateHTML(html)).toBe('<p title="Hallo">Hello &amp; &lt;world&gt; <b translate="no">Hallo</b><script>Hallo</script></p>');
  });
  test('placeholders survive without being passed to the model', async () => {
    const engine = new TranslationEngine() as any;
    engine.translatePlain = async (s: string) => s.toUpperCase();
    expect(await engine.translateText('hello {0} and [1]')).toBe('HELLO {0} AND [1]');
  });
  test('long text is split even when token count fits, without dropping sentences', async () => {
    const engine = new TranslationEngine() as any;
    engine.tokenLimit = 20;
    engine.src = { encode: (s: string) => [1], eos: 0 };
    engine.dst = { eos: 0, decode: () => 'Hello!' };
    engine.session = { run: async () => ({Y:{data:BigInt64Array.of(1n,0n),dispose(){}}}) };
    expect(await engine.translatePlain('Hallo! '.repeat(10))).toBe('Hello! '.repeat(10));
  });
  test('closing waits for queued requests and rejects new ones', async () => {
    const engine = new TranslationEngine() as any;
    let release = false;
    engine.session = { release: async () => { release = true; } };
    engine.translateText = async (s: string) => { await new Promise(r => setTimeout(r, 10)); expect(release).toBe(false); return s; };
    const request = engine.translateAsync('text');
    const closing = engine.destroy();
    await expect(engine.translateAsync('late')).rejects.toThrow('closing');
    expect(await request).toBe('text'); await closing; expect(release).toBe(true);
  });
  test('CPU requests respect the measured concurrency and drain after an error before closing', async () => {
    const engine = new TranslationEngine() as any;
    let active = 0, peak = 0, released = false;
    engine.backend = { provider: 'cpu', maxConcurrentRuns: 2 };
    engine.session = { release: async () => { expect(active).toBe(0); released = true; } };
    engine.translateText = async (text: string) => {
      active++; peak = Math.max(peak, active);
      try {
        await new Promise(resolve => setTimeout(resolve, 5));
        expect(released).toBe(false);
        if (text === '2') throw new Error('request failed');
        return text;
      } finally { active--; }
    };
    const requests = Promise.allSettled(Array.from({ length: 8 }, (_, i) => engine.translateAsync(String(i))));
    const closing = engine.destroy();
    expect(engine.busy).toBe(true);
    const results = await requests;
    await closing;
    expect(peak).toBe(2);
    expect(results.filter(result => result.status === 'rejected')).toHaveLength(1);
    expect(results.filter(result => result.status === 'fulfilled').map(result => result.value)).toEqual(['0', '1', '3', '4', '5', '6', '7']);
    expect(engine.busy).toBe(false);
    expect(released).toBe(true);
  });
});

describe('HTML inference scheduling', () => {
  function setup(limit = 2) {
    const engine = new TranslationEngine() as any;
    let active = 0, peak = 0, calls = 0, released = false;
    engine.backend = {provider:'cpu',runtime:'native-async',maxConcurrentRuns:limit};
    engine.src = {encode:()=>[5],eos:2,close(){expect(active).toBe(0);}};
    engine.dst = {eos:2,decode:()=> 'translated & <text>',close(){expect(active).toBe(0);}};
    engine.session = {
      run: async () => {
        active++; calls++; peak=Math.max(peak,active);
        try { await new Promise(resolve=>setTimeout(resolve,2));expect(released).toBe(false);return {Y:{data:BigInt64Array.of(8n,2n),dispose(){}}}; }
        finally {active--;}
      },
      release:async()=>{expect(active).toBe(0);released=true;},
    };
    return {engine,stats:()=>({active,peak,calls,released})};
  }
  test.each([1,2,4])('multiple HTML requests share the inference limit %i and drain on close',async limit=>{
    const {engine,stats}=setup(limit);
    const html='<p title="same">one</p><b>two</b><i>three</i><em class="notranslate">four</em><script>five</script>';
    const output='<p title="same">translated &amp; &lt;text&gt;</p><b>translated &amp; &lt;text&gt;</b><i>translated &amp; &lt;text&gt;</i><em class="notranslate">four</em><script>five</script>';
    const requests=Promise.all(Array.from({length:5},()=>engine.translateAsync(html,{html:true})));
    const closing=engine.destroy();
    await expect(engine.translateAsync('late')).rejects.toThrow('closing');
    expect(await requests).toEqual(Array(5).fill(output));await closing;
    expect(stats()).toEqual({active:0,peak:limit,calls:15,released:true});expect(engine.busy).toBe(false);
  });
  test('an HTML failure stops new parts and waits for already running parts before release',async()=>{
    const {engine,stats}=setup();let finish!:()=>void,failed!:()=>void;
    const failureStarted=new Promise<void>(resolve=>failed=resolve),pending=new Promise<void>(resolve=>finish=resolve);
    let calls=0;
    engine.session.run=async()=>{
      calls++;
      if(calls===1){failed();throw new Error('part failed');}
      await pending;
      return {Y:{data:BigInt64Array.of(8n,2n),dispose(){}}};
    };
    const request=engine.translateAsync('<p>one</p><p>two</p><p>three</p>',{html:true});
    const result=request.catch((error:Error)=>error.message);
    await failureStarted;await new Promise(resolve=>setTimeout(resolve,0));
    const closing=engine.destroy();let closed=false;void closing.then(()=>closed=true);
    await new Promise(resolve=>setTimeout(resolve,0));expect(closed).toBe(false);expect(engine.busy).toBe(true);expect(calls).toBe(2);
    finish();expect(await result).toBe('part failed');await closing;expect(calls).toBe(2);expect(stats().released).toBe(true);
  });
  test('queued inference respects a reduced backend limit',async()=>{
    const {engine}=setup(2);let active=0,peakAfterDrain=0,started=0;
    engine.session.run=async()=>{
      active++;started++;
      if(started===2)engine.backend.maxConcurrentRuns=1;
      if(started>2)peakAfterDrain=Math.max(peakAfterDrain,active);
      try{await new Promise(resolve=>setTimeout(resolve,2));return {Y:{data:BigInt64Array.of(8n,2n),dispose(){}}};}finally{active--;}
    };
    await Promise.all(Array.from({length:3},()=>engine.translateAsync('<p>one</p><p>two</p><p>three</p>',{html:true})));
    await engine.destroy();expect(started).toBe(9);expect(peakAfterDrain).toBe(1);
  });
});

test('competing requests suppress extra HTML workers',async()=>{
 const engine=new TranslationEngine() as any;let active=0,peak=0,calls=0,finish!:()=>void;
 const gate=new Promise<void>(resolve=>finish=resolve);
 engine.backend={provider:'cpu',maxConcurrentRuns:4};engine.session={release:async()=>{expect(active).toBe(0);}};
 engine.translateText=async(text:string)=>{active++;calls++;peak=Math.max(peak,active);try{await gate;return text;}finally{active--;}};
 const plain=engine.translateAsync('hold'),html=engine.translateAsync('<p>one</p><b>two</b><i>three</i>',{html:true});
 await Promise.resolve();expect(calls).toBe(2);finish();
 expect(await plain).toBe('hold');expect(await html).toBe('<p>one</p><b>two</b><i>three</i>');
 await engine.destroy();expect(peak).toBe(2);expect(calls).toBe(4);
});
