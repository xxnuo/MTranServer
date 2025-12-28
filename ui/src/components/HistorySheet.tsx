import { useState, useMemo } from "react"
import {
  Sheet,
  SheetContent,
  SheetHeader,
  SheetTitle,
  SheetDescription,
  SheetFooter
} from "@/components/ui/sheet"
import { ScrollArea } from "@/components/ui/scroll-area"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import { Trash2, Search } from "lucide-react"
import type { HistoryItem } from "@/hooks/use-history"
import { useTranslation } from "react-i18next"

interface HistorySheetProps {
  open: boolean
  onOpenChange: (open: boolean) => void
  history: HistoryItem[]
  onSelect: (item: HistoryItem) => void
  onClear: () => void
  onDelete: (id: string) => void
}

export function HistorySheet({
  open,
  onOpenChange,
  history,
  onSelect,
  onClear,
  onDelete,
}: HistorySheetProps) {
  const { t } = useTranslation()
  const [searchQuery, setSearchQuery] = useState("")

  const filteredHistory = useMemo(() => {
    if (!searchQuery.trim()) return history
    const lowerQuery = searchQuery.toLowerCase()
    return history.filter(
      (item) =>
        item.sourceText.toLowerCase().includes(lowerQuery) ||
        item.translatedText.toLowerCase().includes(lowerQuery)
    )
  }, [history, searchQuery])

  return (
    <Sheet open={open} onOpenChange={onOpenChange}>
      <SheetContent side="right" className="w-[300px] sm:w-[400px] flex flex-col gap-0">
        <SheetHeader className="pb-4">
          <SheetTitle>{t('history')}</SheetTitle>
          <SheetDescription>
            {history.length === 0 ? t('noHistory') : t('historyDesc')}
          </SheetDescription>
        </SheetHeader>
        
        {history.length > 0 && (
          <div className="px-4 pb-4">
            <div className="relative">
                <Search className="absolute left-2 top-2.5 h-4 w-4 text-muted-foreground pointer-events-none" />
                <Input 
                    placeholder={t('searchPlaceholder')} 
                    className="pl-8" 
                    value={searchQuery}
                    onChange={e => setSearchQuery(e.target.value)}
                />
            </div>
          </div>
        )}

        <ScrollArea className="flex-1 px-4">
          <div className="flex flex-col gap-4 pb-4">
            {filteredHistory.map((item) => (
              <div
                key={item.id}
                className="relative flex flex-col gap-2 rounded-lg border p-3 hover:bg-accent cursor-pointer transition-colors group"
                onClick={() => {
                   onSelect(item)
                   onOpenChange(false)
                }}
              >
                <div className="flex justify-between text-xs text-muted-foreground">
                    <span>{item.from} → {item.to}</span>
                    <Button
                        variant="ghost"
                        size="icon"
                        className="h-6 w-6 -mr-1 text-muted-foreground hover:text-destructive z-10 opacity-0 group-hover:opacity-100 transition-opacity"
                        onClick={(e) => {
                            e.stopPropagation()
                            onDelete(item.id)
                        }}
                    >
                        <Trash2 className="h-3 w-3" />
                    </Button>
                </div>
                <div className="text-sm line-clamp-2 font-medium break-all">{item.sourceText}</div>
                <div className="text-sm text-muted-foreground line-clamp-2 break-all">{item.translatedText}</div>
              </div>
            ))}
            {filteredHistory.length === 0 && searchQuery && (
                <div className="text-center text-sm text-muted-foreground py-8">
                    {t('noResults')}
                </div>
            )}
          </div>
        </ScrollArea>

        {history.length > 0 && (
            <SheetFooter className="p-4 border-t mt-auto">
                <Button variant="destructive" className="w-full" onClick={onClear}>
                    {t('clearAll')}
                </Button>
            </SheetFooter>
        )}
      </SheetContent>
    </Sheet>
  )
}
