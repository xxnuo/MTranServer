import { useState, useEffect } from 'react'

export interface HistoryItem {
  id: string
  from: string
  to: string
  sourceText: string
  translatedText: string
  timestamp: number
}

const HISTORY_KEY = 'translation_history'
const MAX_HISTORY = 50

export function useHistory() {
  const [history, setHistory] = useState<HistoryItem[]>([])

  useEffect(() => {
    try {
      const stored = localStorage.getItem(HISTORY_KEY)
      if (stored) {
        setHistory(JSON.parse(stored))
      }
    } catch (e) {
      console.error('Failed to load history', e)
    }
  }, [])

  const addToHistory = (item: Omit<HistoryItem, 'id' | 'timestamp'>) => {
    const newItem: HistoryItem = {
      ...item,
      id: `${Date.now()}-${Math.random().toString(36).substring(2, 11)}`,
      timestamp: Date.now(),
    }

    setHistory((prev) => {
      // Remove duplicates if same source text and languages
      const filtered = prev.filter(
        (h) =>
          !(
            h.sourceText === item.sourceText &&
            h.from === item.from &&
            h.to === item.to
          )
      )
      const updated = [newItem, ...filtered].slice(0, MAX_HISTORY)
      localStorage.setItem(HISTORY_KEY, JSON.stringify(updated))
      return updated
    })
  }

  const clearHistory = () => {
    setHistory([])
    localStorage.removeItem(HISTORY_KEY)
  }

  const deleteItem = (id: string) => {
    setHistory((prev) => {
      const updated = prev.filter((item) => item.id !== id)
      localStorage.setItem(HISTORY_KEY, JSON.stringify(updated))
      return updated
    })
  }

  return { history, addToHistory, clearHistory, deleteItem }
}
