package utils

import (
	"strconv"
	"strings"
)

// 比较版本号，返回最大的版本号
// 支持 semver 格式，但不完全符合 semver 规范
// 样例：
// - 1.0.0, 1.0.1, 1.1.0 返回 1.1.0
// - 1.0.0-alpha.1, 1.0.0-alpha.2, 1.0.0-alpha.3 返回 1.0.0-alpha.3
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

// compareVersions 比较两个版本号
// 返回值: v1 > v2 返回 1, v1 < v2 返回 -1, v1 == v2 返回 0
func compareVersions(v1, v2 string) int {
	// 分割版本号和预发布标识
	parts1 := strings.Split(v1, "-")
	parts2 := strings.Split(v2, "-")

	version1 := parts1[0]
	version2 := parts2[0]

	// 比较主版本号
	cmp := compareNumericVersions(version1, version2)
	if cmp != 0 {
		return cmp
	}

	// 版本号相同，比较预发布标识
	// 无预发布标识 > 有预发布标识
	if len(parts1) == 1 && len(parts2) > 1 {
		return 1
	}
	if len(parts1) > 1 && len(parts2) == 1 {
		return -1
	}
	if len(parts1) == 1 && len(parts2) == 1 {
		return 0
	}

	// 比较预发布标识
	return comparePrerelease(parts1[1], parts2[1])
}

// compareNumericVersions 比较数字版本号 (如 1.0.0)
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

// comparePrerelease 比较预发布标识
func comparePrerelease(p1, p2 string) int {
	// 分割预发布标识的各个部分
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

		// 尝试作为数字比较
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
			// 字符串比较
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
