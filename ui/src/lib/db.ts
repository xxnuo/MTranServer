import { openDB, type DBSchema, type IDBPDatabase } from 'idb';

interface HistoryItem {
    id: string;
    from: string;
    to: string;
    sourceText: string;
    translatedText: string;
    timestamp: number;
}

interface MTranHistoryDB extends DBSchema {
    history: {
        key: string;
        value: HistoryItem;
        indexes: { 'timestamp': number };
    };
}

const DB_NAME = 'MTranHistoryDB';
const STORE_NAME = 'history';
const LOCAL_STORAGE_KEY = 'translation_history';

class DbService {
    private dbPromise: Promise<IDBPDatabase<MTranHistoryDB>>;

    constructor() {
        this.dbPromise = openDB<MTranHistoryDB>(DB_NAME, 1, {
            upgrade(db) {
                const store = db.createObjectStore(STORE_NAME, { keyPath: 'id' });
                store.createIndex('timestamp', 'timestamp');
            },
        });
    }

    async migrateFromLocalStorage() {
        try {
            const stored = localStorage.getItem(LOCAL_STORAGE_KEY);
            if (stored) {
                const items: HistoryItem[] = JSON.parse(stored);
                if (Array.isArray(items) && items.length > 0) {
                    const db = await this.dbPromise;
                    const tx = db.transaction(STORE_NAME, 'readwrite');
                    const store = tx.objectStore(STORE_NAME);

                    for (const item of items) {
                        await store.put(item);
                    }
                    await tx.done;
                    console.log(`Migrated ${items.length} items from localStorage to IndexedDB`);
                }
                localStorage.removeItem(LOCAL_STORAGE_KEY);
            }
        } catch (e) {
            console.error('Migration failed:', e);
        }
    }

    async getAll(limit: number = 20, offset: number = 0): Promise<HistoryItem[]> {
        const db = await this.dbPromise;
        const items: HistoryItem[] = [];

        let cursor = await db.transaction(STORE_NAME).store.index('timestamp').openCursor(null, 'prev');

        if (offset > 0 && cursor) {
            try {
                await cursor.advance(offset);
            } catch (e) {
                cursor = null
            }
        }

        while (cursor && items.length < limit) {
            items.push(cursor.value);
            cursor = await cursor.continue();
        }

        return items;
    }

    async add(item: HistoryItem) {
        const db = await this.dbPromise;
        await db.put(STORE_NAME, item);
    }

    async delete(id: string) {
        const db = await this.dbPromise;
        await db.delete(STORE_NAME, id);
    }

    async clear() {
        const db = await this.dbPromise;
        await db.clear(STORE_NAME);
    }

    async search(query: string, limit: number = 50): Promise<HistoryItem[]> {
        const db = await this.dbPromise;
        const lowerQuery = query.toLowerCase();
        const items: HistoryItem[] = [];

        let cursor = await db.transaction(STORE_NAME).store.index('timestamp').openCursor(null, 'prev');

        while (cursor && items.length < limit) {
            const item = cursor.value;
            if (
                item.sourceText.toLowerCase().includes(lowerQuery) ||
                item.translatedText.toLowerCase().includes(lowerQuery)
            ) {
                items.push(item);
            }
            cursor = await cursor.continue();
        }

        return items;
    }
}

export const dbService = new DbService();
export type { HistoryItem };
