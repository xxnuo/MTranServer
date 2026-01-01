package utils

import (
	"strconv"
	"strings"
)

func GetLargestVersion(versions []string) string {
	if len(versions) == 0 {
		return ""
	}

	largest := versions[0]
	for _, v := range versions[1:] {
		if compareVersions(v, largest) > 0 {
			largest = v
		}
	}
	return largest
}

func compareVersions(v1, v2 string) int {

	parts1 := strings.Split(v1, "-")
	parts2 := strings.Split(v2, "-")

	version1 := parts1[0]
	version2 := parts2[0]

	cmp := compareNumericVersions(version1, version2)
	if cmp != 0 {
		return cmp
	}

	if len(parts1) == 1 && len(parts2) > 1 {
		return 1
	}
	if len(parts1) > 1 && len(parts2) == 1 {
		return -1
	}
	if len(parts1) == 1 && len(parts2) == 1 {
		return 0
	}

	return comparePrerelease(parts1[1], parts2[1])
}

func compareNumericVersions(v1, v2 string) int {
	segments1 := strings.Split(v1, ".")
	segments2 := strings.Split(v2, ".")

	maxLen := len(segments1)
	if len(segments2) > maxLen {
		maxLen = len(segments2)
	}

	for i := 0; i < maxLen; i++ {
		var num1, num2 int

		if i < len(segments1) {
			num1, _ = strconv.Atoi(segments1[i])
		}
		if i < len(segments2) {
			num2, _ = strconv.Atoi(segments2[i])
		}

		if num1 > num2 {
			return 1
		}
		if num1 < num2 {
			return -1
		}
	}

	return 0
}

func comparePrerelease(p1, p2 string) int {

	parts1 := strings.Split(p1, ".")
	parts2 := strings.Split(p2, ".")

	maxLen := len(parts1)
	if len(parts2) > maxLen {
		maxLen = len(parts2)
	}

	for i := 0; i < maxLen; i++ {
		if i >= len(parts1) {
			return -1
		}
		if i >= len(parts2) {
			return 1
		}

		part1 := parts1[i]
		part2 := parts2[i]

		num1, err1 := strconv.Atoi(part1)
		num2, err2 := strconv.Atoi(part2)

		if err1 == nil && err2 == nil {
			if num1 > num2 {
				return 1
			}
			if num1 < num2 {
				return -1
			}
		} else {

			if part1 > part2 {
				return 1
			}
			if part1 < part2 {
				return -1
			}
		}
	}

	return 0
}
