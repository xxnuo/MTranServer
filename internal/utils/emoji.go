package utils

import (
	"fmt"
	"regexp"
	"strings"
)

var emojiRegex = regexp.MustCompile(`(?:[\x{1F1E0}-\x{1F1FF}]{2}|[\x{1F600}-\x{1F64F}\x{1F300}-\x{1F5FF}\x{1F680}-\x{1F6FF}\x{1F700}-\x{1F77F}\x{1F780}-\x{1F7FF}\x{1F800}-\x{1F8FF}\x{1F900}-\x{1F9FF}\x{1FA00}-\x{1FA6F}\x{1FA70}-\x{1FAFF}\x{2600}-\x{26FF}\x{2700}-\x{27BF}\x{1F191}-\x{1F251}\x{1F004}\x{1F0CF}\x{1F18E}\x{3030}\x{2B50}\x{2B55}\x{2934}\x{2935}\x{2B05}-\x{2B07}\x{2B1B}\x{2B1C}\x{3297}\x{3299}\x{303D}\x{00A9}\x{00AE}\x{2122}\x{23F0}\x{23F3}\x{2328}\x{23CF}\x{23E9}-\x{23F3}\x{23F8}-\x{23FA}\x{24C2}\x{25AA}\x{25AB}\x{25B6}\x{25C0}\x{25FB}-\x{25FE}\x{2600}-\x{2604}\x{260E}\x{2611}\x{2614}\x{2615}\x{2618}\x{261D}\x{2620}\x{2622}\x{2623}\x{2626}\x{262A}\x{262E}\x{262F}\x{2638}-\x{263A}\x{2640}\x{2642}\x{2648}-\x{2653}\x{2660}\x{2663}\x{2665}\x{2666}\x{2668}\x{267B}\x{267F}\x{2692}-\x{2697}\x{2699}\x{269B}\x{269C}\x{26A0}\x{26A1}\x{26AA}\x{26AB}\x{26B0}\x{26B1}\x{26BD}\x{26BE}\x{26C4}\x{26C5}\x{26C8}\x{26CE}\x{26CF}\x{26D1}\x{26D3}\x{26D4}\x{26E9}\x{26EA}\x{26F0}-\x{26F5}\x{26F7}-\x{26FA}\x{26FD}\x{2702}\x{2705}\x{2708}-\x{270D}\x{270F}\x{2712}\x{2714}\x{2716}\x{271D}\x{2721}\x{2728}\x{2733}\x{2734}\x{2744}\x{2747}\x{274C}\x{274E}\x{2753}-\x{2755}\x{2757}\x{2763}\x{2764}\x{2795}-\x{2797}\x{27A1}\x{27B0}\x{27BF}\x{2B50}\x{2B55}\x{3030}\x{303D}\x{3297}\x{3299}])`)

type emojiReplacement struct {
	original    string
	placeholder string
}

func ProcessTextWithEmojiHandling(text string, processFunc func(string) (string, error)) (string, error) {
	var replacements []emojiReplacement

	cleanText := emojiRegex.ReplaceAllStringFunc(text, func(emoji string) string {
		placeholder := fmt.Sprintf("[E%d]", len(replacements))
		replacements = append(replacements, emojiReplacement{
			original:    emoji,
			placeholder: placeholder,
		})
		return placeholder
	})

	processedText, err := processFunc(cleanText)
	if err != nil {
		return "", err
	}

	result := processedText
	for _, r := range replacements {
		result = strings.Replace(result, r.placeholder, r.original, 1)
	}

	return result, nil
}
