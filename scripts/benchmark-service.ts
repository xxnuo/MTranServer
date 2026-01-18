import { readFile, writeFile } from 'node:fs/promises';
const [corpus, output, baseline, candidate] = process.argv.slice(2);
if (!candidate) throw new Error('Usage: benchmark-service corpus.jsonl output.json baseline-url candidate-url');
const rows = (await readFile(corpus, 'utf8')).trim().split('\n').slice(0, 64).map(s => JSON.parse(s));
const request = async (url: string, text: string) => {
  const response = await fetch(url + '/translate', { method: 'POST', headers: {'content-type':'application/json'}, body:JSON.stringify({from:'de',to:'en',text}), signal:AbortSignal.timeout(120000) });
  if (!response.ok) throw new Error(`${url}: ${response.status} ${await response.text()}`);
  const result = await response.json() as {result:string}; return result.result;
};
const results: Record<string, {rounds: number[]; times: number[]; texts: string[]}> = {};
for (const url of [baseline,candidate]) { results[url]={rounds:[],times:[],texts:[]}; for(const row of rows.slice(0,8)) await request(url,row.source); }
for(let round=0;round<3;round++) for(const url of round%2?[candidate,baseline]:[baseline,candidate]) {
  const started=performance.now();
  for(let i=0;i<rows.length;i++) { const start=performance.now();const text=await request(url,rows[i].source);results[url].times.push(performance.now()-start);if(round===0)results[url].texts.push(text);else if(text!==results[url].texts[i])throw new Error('Non-deterministic output'); }
  const rate=rows.length/((performance.now()-started)/1000);results[url].rounds.push(rate); console.log(url,round,rate.toFixed(2),'sentences/s');
}
const summary=Object.fromEntries(Object.entries(results).map(([url,r])=>[url,{...r,median:[...r.rounds].sort((a,b)=>a-b)[1],p95:[...r.times].sort((a,b)=>a-b)[Math.floor(r.times.length*.95)]}]));
await writeFile(output,JSON.stringify(summary,null,2));console.log(JSON.stringify(Object.fromEntries(Object.entries(summary).map(([k,v])=>[k,{median:v.median,p95:v.p95}]))));
