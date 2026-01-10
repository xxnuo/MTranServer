import { useState, useEffect, useCallback } from 'react'
import { dbService, type HistoryItem } from '@/lib/db'

const PAGE_SIZE = 20

export type { HistoryItem }

export function useHistory() {
  const [history, setHistory] = useState<HistoryItem[]>([])
  const [isLoading, setIsLoading] = useState(true)
  const [hasMore, setHasMore] = useState(true)
  const [page, setPage] = useState(0)
  const [searchQuery, setSearchQuery] = useState('')

  // Initial load and migration
  useEffect(() => {
    const init = async () => {
      setIsLoading(true)
      await dbService.migrateFromLocalStorage()
      const items = await dbService.getAll(PAGE_SIZE, 0)
      setHistory(items)
      setHasMore(items.length >= PAGE_SIZE)
      setIsLoading(false)
    }
    init()
  }, [])

  // Load more function
  const loadMore = useCallback(async () => {
    if (isLoading || !hasMore) return

    setIsLoading(true)
    try {
      const nextPage = page + 1
      const offset = nextPage * PAGE_SIZE

      let newItems: HistoryItem[]

      if (searchQuery) {
        newItems = []
      } else {
        newItems = await dbService.getAll(PAGE_SIZE, offset)
      }

      if (newItems.length > 0) {
        setHistory(prev => [...prev, ...newItems])
        setPage(nextPage)
        setHasMore(newItems.length >= PAGE_SIZE)
      } else {
        setHasMore(false)
      }
    } catch (error) {
      console.error('Failed to load more history:', error)
    } finally {
      setIsLoading(false)
    }
  }, [page, isLoading, hasMore, searchQuery])

  const search = useCallback(async (query: string) => {
    setSearchQuery(query)
    setPage(0)
    setIsLoading(true)
    try {
      if (!query.trim()) {
        const items = await dbService.getAll(PAGE_SIZE, 0)
        setHistory(items)
        setHasMore(items.length >= PAGE_SIZE)
      } else {
        const items = await dbService.search(query, 50)
        setHistory(items)
        setHasMore(false)
      }
    } catch (e) {
      console.error('Search failed', e)
    } finally {
      setIsLoading(false)
    }
  }, [])

  const addToHistory = useCallback(async (item: Omit<HistoryItem, 'id' | 'timestamp'>) => {
    const newItem: HistoryItem = {
      ...item,
      id: `${Date.now()}-${Math.random().toString(36).substring(2, 11)}`,
      timestamp: Date.now(),
    }

    try {
      await dbService.add(newItem)
      setHistory((prev) => {
        const filtered = prev.filter(
          (h) => !(h.sourceText === item.sourceText && h.from === item.from && h.to === item.to)
        )

        return [newItem, ...filtered]
      })
    } catch (e) {
      console.error('Failed to add to history', e)
    }
  }, [])

  const clearHistory = useCallback(async () => {
    try {
      await dbService.clear()
      setHistory([])
      setPage(0)
      setHasMore(false)
    } catch (e) {
      console.error('Failed to clear history', e)
    }
  }, [])

  const deleteItem = useCallback(async (id: string) => {
    try {
      await dbService.delete(id)
      setHistory((prev) => prev.filter((item) => item.id !== id))
    } catch (e) {
      console.error('Failed to delete item', e)
    }
  }, [])

  return {
    history,
    addToHistory,
    clearHistory,
    deleteItem,
    loadMore,
    hasMore,
    isLoading,
    search
  }
}
