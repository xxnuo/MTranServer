export function getLargestVersion(versions: string[]): string {
  if (versions.length === 0) return '';
  if (versions.length === 1) return versions[0];

  return versions.reduce((max, current) => {
    return compareVersions(current, max) > 0 ? current : max;
  });
}

function compareVersions(v1: string, v2: string): number {
  const parts1 = v1.split('.').map(p => parseInt(p, 10) || 0);
  const parts2 = v2.split('.').map(p => parseInt(p, 10) || 0);

  const maxLength = Math.max(parts1.length, parts2.length);

  for (let i = 0; i < maxLength; i++) {
    const p1 = parts1[i] || 0;
    const p2 = parts2[i] || 0;

    if (p1 > p2) return 1;
    if (p1 < p2) return -1;
  }

  return 0;
}
