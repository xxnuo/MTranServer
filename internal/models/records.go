package models

const (
	recordsUrl         = "https://remote-settings.mozilla.org/v1/buckets/main/collections/translations-models/records"
	attachmentsBaseUrl = "https://firefox-settings-attachments.cdn.mozilla.net"
)

// 检测默认配置目录下是否存在 records.json
// 存在则解析本地 records.json
// 不存在则写出默认内嵌的 records.json 到配置目录然后解析
func initRecords() error {
	return nil
}

// 更新 records.json，从远程下载 records.json 到配置目录
// 然后解析本地 records.json
func downloadRecords() error {
	return nil
}

// 解析 records.json，找到对应的模型属性
// 检查配置目录下是否存在对应的模型文件，可通过 sha256 校验
// 不存在则下载到配置目录
// 参数：toLang 目标语言，fromLang 源语言，version 模型版本
// records 里同一个 fromLang 和 toLang 会存在多个 version 的模型
// 需要根据 version 下载对应的模型，未指定 version 则下载最新版本
func downloadModel(toLang string, fromLang string, version string) error {
	return nil
}
