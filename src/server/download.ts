import * as logger from '@/logger/index.js';
import { RecordsData } from '@/models';
import { NormalizeLanguageCode } from '@/utils/index.js';

export async function DownloadCommand(globalRecords: RecordsData, downloadModel: Function) {
    const downloadIndex = process.argv.indexOf('--download');
    const inputPairs: string[] = [];

    for (let i = downloadIndex + 1; i < process.argv.length; i++) {
        const arg = process.argv[i];
        if (arg.startsWith('-')) break;
        inputPairs.push(arg);
    }

    if (inputPairs.length === 0) {
        logger.error('Please specify at least one language pair (e.g., --download en-zh)');
        process.exit(1);
    }

    const availablePairs: { from: string; to: string }[] = [];
    const recordsData = globalRecords.data;

    for (const inputPair of inputPairs) {
        const delimiter = inputPair.includes('_') ? '_' : '-';
        const parts = inputPair.split(delimiter);

        if (parts.length !== 2) {
            logger.warn(`Invalid language pair: ${inputPair}. Use format from-to or from_to.`);
            continue;
        }

        const fromLang = NormalizeLanguageCode(parts[0]);
        const toLang = NormalizeLanguageCode(parts[1]);

        const exactMatch = recordsData.find(r =>
            NormalizeLanguageCode(r.sourceLanguage) === fromLang &&
            NormalizeLanguageCode(r.targetLanguage) === toLang
        );

        if (exactMatch) {
            availablePairs.push({ from: exactMatch.sourceLanguage, to: exactMatch.targetLanguage });
        } else {
            logger.warn(`Could not find model for pair: ${inputPair} (normalized: ${fromLang}_${toLang})`);
        }
    }

    if (availablePairs.length === 0) {
        logger.warn('No valid models found to download.');
    } else {
        logger.info(`Found ${availablePairs.length} pair(s) to download...`);
        const unique = availablePairs.filter((v, i, a) => a.findIndex(t => t.from === v.from && t.to === v.to) === i);

        for (const pair of unique) {
            await downloadModel(pair.to, pair.from);
        }
    }
    process.exit(0);

}