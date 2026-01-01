import { NormalizeLanguageCode } from '@/utils/index.js';
export async function LanguagesCommand(getLanguagePairs: Function) {
    const argIndex = process.argv.findIndex(a => a === '--languages' || a === '--language');
    const filterArg = process.argv[argIndex + 1];
    const filter = filterArg && !filterArg.startsWith('-') ? NormalizeLanguageCode(filterArg) : null;

    const pairs = getLanguagePairs();
    console.log('Available language pairs:');
    let currentRow = '';
    for (const pair of pairs.sort()) {
        const [from, to] = pair.split('_');
        if (filter && NormalizeLanguageCode(from) !== filter && NormalizeLanguageCode(to) !== filter) {
            continue;
        }

        if (currentRow.length > 80) {
            console.log(currentRow);
            currentRow = '';
        }
        currentRow += pair + '  ';
    }
    if (currentRow) console.log(currentRow);
    process.exit(0);
}