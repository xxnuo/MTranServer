const Translator = require("@mtran/core");

const loadedLangs = new Set();
let supportedLanguages = Translator.GetSupportLanguages();

function checkSupportLanguage(from, to) {
  return (
    (supportedLanguages.includes(from) || from === "auto") &&
    supportedLanguages.includes(to)
  );
}

/**
 * 预加载翻译模型
 * @param {string} from 源语言
 * @param {string} to 目标语言
 * @returns {Promise<void>}
 */
async function preloadModel(from, to) {
  if (!checkSupportLanguage(from, to)) {
    throw new Error("Unsupported language pair");
  }
  const modelKey = `${from}_${to}`;
  if (!loadedLangs.has(modelKey)) {
    await Translator.Preload(from, to);
    loadedLangs.add(modelKey);
    console.log(`Successfully loaded model for language pair: ${modelKey}`);
  }
}

/**
 * 翻译文本
 * @param {string} text 要翻译的文本
 * @param {string} from 源语言
 * @param {string} to 目标语言
 * @returns {Promise<string>} 翻译结果
 */
async function translate(text, from, to) {
  if (!checkSupportLanguage(from, to)) {
    throw new Error("Unsupported language pair");
  }
  return Translator.Translate(text, from, to);
}

/**
 * 批量翻译文本
 * @param {string[]} texts 要翻译的文本数组
 * @param {string} from 源语言
 * @param {string} to 目标语言
 * @returns {Promise<string[]>} 翻译结果数组
 */
async function batchTranslate(texts, from, to) {
  if (!checkSupportLanguage(from, to)) {
    throw new Error("Unsupported language pair");
  }
  // 并行处理翻译请求
  const promises = texts.map((text) => Translator.Translate(text, from, to));
  return Promise.all(promises);
}

/**
 * 关闭翻译引擎
 * @returns {Promise<void>}
 */
async function shutdown() {
  await Translator.Shutdown();
  console.log("Translation engine shutdown complete");
}

module.exports = {
  supportedLanguages,
  preloadModel,
  translate,
  batchTranslate,
  shutdown,
};
