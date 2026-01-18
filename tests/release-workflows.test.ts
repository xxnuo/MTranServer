import { expect, test } from 'bun:test';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import path from 'node:path';
import YAML from 'yaml';
import pkg from '../package.json';

for (const [file, job, name, key] of [
  ['build.yml', 'release', 'Determine Release Tag', 'tag_name'],
  ['docker.yml', 'docker', 'Get latest tag', 'tag'],
]) {
  test.each(['refs/heads/main', 'refs/tags/v9.8.7'])(`${file} publishes the built version: %s`, ref => {
    const workflow = YAML.parse(readFileSync(`.github/workflows/${file}`, 'utf8'));
    const script = workflow.jobs[job].steps.find((step: { name: string }) => step.name === name).run
      .replaceAll('${{ github.event_name }}', ref.startsWith('refs/tags/') ? 'push' : 'workflow_dispatch')
      .replaceAll('${{ github.ref_name }}', ref.split('/').at(-1));
    const directory = mkdtempSync(path.resolve('.tmp/release-workflow-'));
    const output = path.join(directory, 'output');
    try {
      execFileSync('bash', ['-e', '-c', script], {
        env: { ...process.env, GITHUB_REF: ref, GITHUB_OUTPUT: output }, stdio: 'pipe',
      });
      expect(readFileSync(output, 'utf8').trim()).toBe(`${key}=${ref.startsWith('refs/tags/') ? 'v9.8.7' : `v${pkg.version}`}`);
    } finally { rmSync(directory, { recursive: true, force: true }); }
  });
}

test.each(['refs/heads/main', 'refs/tags/v9.8.7'])('npm version follows the release tag: %s', ref => {
  const workflow = YAML.parse(readFileSync('.github/workflows/npm.yml', 'utf8'));
  const script = workflow.jobs.publish.steps.find((step: { name: string }) => step.name === 'Sync version from tag').run;
  const stdout = execFileSync('bash', ['-e', '-c', `bun() { printf '%s\\n' "$*"; }; ${script}`], {
    env: { ...process.env, GITHUB_REF: ref }, encoding: 'utf8',
  });
  expect(stdout.trim()).toBe(ref.startsWith('refs/tags/') ? 'run bump v9.8.7' : '');
});
