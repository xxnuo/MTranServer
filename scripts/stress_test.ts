import fs from 'fs/promises';
import path from 'path';
import os from 'os';
import { exec } from 'child_process';
import util from 'util';

const execAsync = util.promisify(exec);

const SERVER_URL = 'http://localhost:8989/translate';
const PROCESS_NAME = 'mtranserver';
const LOOPS = 5;

async function getPidByName(name: string): Promise<number | null> {
    try {
        const { stdout } = await execAsync(`pgrep -f "${name}"`);
        const pids = stdout.trim().split('\n').map(Number);
        const myPid = process.pid;
        const validPids = pids.filter(p => p !== myPid);
        
        if (validPids.length > 0) {
            return Math.min(...validPids);
        }
    } catch (e) {
    }

    try {
        const { stdout } = await execAsync('ps aux');
        const lines = stdout.split('\n');
        for (const line of lines) {
            if (line.includes(name) && !line.includes('stress_test') && !line.includes(String(process.pid))) {
                const parts = line.trim().split(/\s+/);
                const pid = parseInt(parts[1], 10);
                if (!isNaN(pid)) return pid;
            }
        }
    } catch (e) {
        console.error('Failed to find PID:', e);
    }
    return null;
}

async function getProcessMemory(pid: number) {
    try {
        const { stdout } = await execAsync(`ps -o rss,vsz -p ${pid} | tail -n 1`);
        const [rss, vsz] = stdout.trim().split(/\s+/).map(Number);
        return { 
            rssMB: (rss / 1024).toFixed(2), 
            vszMB: (vsz / 1024).toFixed(2) 
        };
    } catch (e) {
        return { rssMB: '?', vszMB: '?' };
    }
}

async function main() {
    console.log(`Starting stress test against ${SERVER_URL}`);
    
    // 1. Find PID
    let serverPid = await getPidByName(PROCESS_NAME);
    if (!serverPid) {
        console.warn(`Could not find process named "${PROCESS_NAME}". Trying to find generic node/bun process listening on 8989...`);
        try {
             const { stdout } = await execAsync("lsof -t -i:8989");
             const pid = parseInt(stdout.trim(), 10);
             if (!isNaN(pid)) serverPid = pid;
        } catch (e) {
             console.error("Could not find any process on port 8989.");
        }
    }

    if (serverPid) {
        console.log(`Monitoring PID: ${serverPid}`);
    } else {
        console.warn("WARNING: Could not determine Server PID. Memory monitoring will be skipped.");
    }

    // 2. Fetch corpus to Temp Dir
    console.log('Fetching corpus...');
    const corpusUrl = 'https://www.gutenberg.org/cache/epub/1661/pg1661.txt';
    const tempDir = os.tmpdir();
    const corpusPath = path.join(tempDir, `mtranserver_stress_test_${Date.now()}.txt`);
    let corpusText = '';

    try {
        console.log(`Downloading from ${corpusUrl} to ${corpusPath}...`);
        const res = await fetch(corpusUrl);
        if (!res.ok) throw new Error(`Failed to fetch corpus: ${res.statusText}`);
        corpusText = await res.text();
        await fs.writeFile(corpusPath, corpusText);
        console.log(`Corpus loaded. Length: ${corpusText.length} chars.`);

        // 3. Prepare chunks
        const sentences = corpusText.split(/[.!?]\s+/)
            .filter(s => s.trim().length > 10 && s.length < 500);
        console.log(`Split into ${sentences.length} sentences.`);

        // 4. Stress Loop
        let totalChars = 0;
        let iterations = 0;
        let errors = 0;
        const startTime = Date.now();

        let initialMem = {rssMB: '0', vszMB: '0' };
        if (serverPid) {
            initialMem = await getProcessMemory(serverPid);
            console.log(`[Start] Server Memory: RSS=${initialMem.rssMB}MB, VSZ=${initialMem.vszMB}MB`);
        }

        for (let l = 0; l < LOOPS; l++) {
            console.log(`Loop ${l + 1}/${LOOPS}`);
            
            for (const sentence of sentences) {
                try {
                    const res = await fetch(SERVER_URL, {
                        method: 'POST',
                        headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify({
                            from: 'en',
                            to: 'zh-Hans',
                            text: sentence
                        })
                    });

                    if (!res.ok) {
                        const errText = await res.text();
                        throw new Error(`HTTP ${res.status}: ${errText}`);
                    }

                    const data = await res.json();
                    if (!data.result) throw new Error('No result in response');

                    totalChars += sentence.length;
                    iterations++;

                    if (iterations % 50 === 0 && serverPid) {
                        const mem = await getProcessMemory(serverPid);
                        process.stdout.write(`\rIter: ${iterations}, TotalChars: ${totalChars}, Errors: ${errors}, ServerMem: RSS=${mem.rssMB}MB `);
                    } else if (iterations % 50 === 0) {
                        process.stdout.write(`\rIter: ${iterations}, TotalChars: ${totalChars}, Errors: ${errors} `);
                    }

                } catch (e: any) {
                    errors++;
                    console.error(`\nTranslation error: ${e.message}`);
                }
            }
            console.log('\nLoop completed.');
        }

        const endTime = Date.now();
        const durationSec = (endTime - startTime) / 1000;
        
        console.log('\n--- Stress Test Finished ---');
        console.log(`Duration: ${durationSec.toFixed(2)}s`);
        console.log(`Total Requests: ${iterations}`);
        console.log(`Total Characters: ${totalChars}`);
        console.log(`Errors: ${errors}`);
        console.log(`Throughput: ${(iterations / durationSec).toFixed(2)} req/s`);
        console.log(`Throughput: ${(totalChars / durationSec).toFixed(2)} chars/s`);

        if (serverPid) {
            const finalMem = await getProcessMemory(serverPid);
            console.log(`Memory Change: RSS ${initialMem.rssMB}MB -> ${finalMem.rssMB}MB`);
        }

    } catch (e) {
        console.error('Error during stress test:', e);
    } finally {
        // Cleanup
        try {
            await fs.unlink(corpusPath);
            console.log(`Deleted temp corpus file: ${corpusPath}`);
        } catch (e) {
            console.error('Failed to delete temp corpus file:', e);
        }
    }
}

main().catch(console.error);