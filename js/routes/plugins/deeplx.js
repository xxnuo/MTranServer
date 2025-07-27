const { translate, batchTranslate } = require("../../utils/translator");
const { validateToken } = require("../../utils/config");

/**
 * DeepLX 翻译兼容API路由处理
 * @param {Object} fastify Fastify实例
 * @param {Object} options 选项
 */
function deeplPlugin(fastify, options) {
  // DeepL官方API兼容端点
  fastify.post(
    "/deeplx/v2/translate",
    {
      schema: {
        description: "DeepL官方API兼容端点",
        tags: ["plugins"],
        headers: {
          type: "object",
          properties: {
            authorization: {
              type: "string",
              description: "token",
            },
          },
        },
        body: {
          type: "object",
          required: ["text", "target_lang"],
          properties: {
            text: {
              oneOf: [
                { type: "string" },
                { type: "array", items: { type: "string" } },
              ],
              description: "需要翻译的文本，可以是字符串或字符串数组",
            },
            source_lang: { type: "string", description: "源语言代码" },
            target_lang: { type: "string", description: "目标语言代码" },
          },
        },
        response: {
          200: {
            type: "object",
            properties: {
              translations: {
                type: "array",
                items: {
                  type: "object",
                  properties: {
                    detected_source_language: {
                      type: "string",
                      description: "检测到的源语言",
                    },
                    text: { type: "string", description: "翻译结果" },
                  },
                },
              },
            },
          },
        },
      },
    },
    async (request, reply) => {
      try {
        // 验证Authorization头部
        const authHeader = request.headers.authorization;
        if (!validateToken(authHeader)) {
          return reply.code(401).send({
            error: "Unauthorized",
            message: "Invalid or missing token",
          });
        }

        const { text, source_lang, target_lang } = request.body;
        const from = source_lang || "auto";
        const to = target_lang;

        // 处理单个文本或文本数组
        if (Array.isArray(text)) {
          const translatedTexts = await batchTranslate(text, from, to);

          return {
            translations: translatedTexts.map((translatedText) => ({
              detected_source_language:
                from === "auto" ? "AUTO" : from.toUpperCase(),
              text: translatedText,
            })),
          };
        } else {
          const translatedText = await translate(text, from, to);

          return {
            translations: [
              {
                detected_source_language:
                  from === "auto" ? "AUTO" : from.toUpperCase(),
                text: translatedText,
              },
            ],
          };
        }
      } catch (error) {
        reply.code(500).send({
          message: error.message,
        });
      }
    }
  );
}

module.exports = deeplPlugin;
