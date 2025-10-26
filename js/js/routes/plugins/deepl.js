const { translate, batchTranslate } = require("../../utils/translator");
const { validateToken } = require("../../utils/config");

/**
 * DeepLX 翻译兼容API路由处理
 * @param {Object} fastify Fastify实例
 * @param {Object} options 选项
 */
function deeplPlugin(fastify, options) {
  fastify.post(
    "/deepl/v2/translate",
    {
      schema: {
        description: "DeepLX v2 API",
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

  fastify.post(
    "/deepl/translate",
    {
      schema: {
        description: "DeepLX API",
        tags: ["plugins"],
        headers: {
          type: "object",
          properties: {
            authorization: {
              type: "string",
              description: "Bearer token",
            },
          },
        },
        querystring: {
          type: "object",
          properties: {
            token: { type: "string", description: "访问令牌" },
          },
        },
        body: {
          type: "object",
          required: ["text", "source_lang", "target_lang"],
          properties: {
            text: { type: "string", description: "需要翻译的文本" },
            source_lang: { type: "string", description: "源语言代码" },
            target_lang: { type: "string", description: "目标语言代码" },
          },
        },
        response: {
          200: {
            type: "object",
            properties: {
              alternatives: {
                type: "array",
                items: { type: "string" },
                description: "备选翻译结果",
              },
              code: { type: "integer", description: "状态码" },
              data: { type: "string", description: "翻译结果" },
              id: { type: "integer", description: "请求ID" },
              method: { type: "string", description: "翻译方法" },
              source_lang: { type: "string", description: "源语言代码" },
              target_lang: { type: "string", description: "目标语言代码" },
            },
          },
        },
      },
    },
    async (request, reply) => {
      try {
        // 验证Authorization头部或URL参数中的token
        const authHeader = request.headers.authorization;
        const urlToken = request.query.token;

        // 从Bearer token中提取token值
        let tokenFromHeader = null;
        if (authHeader && authHeader.startsWith("Bearer ")) {
          tokenFromHeader = authHeader.substring(7);
        }

        // 检查token是否有效
        if (!(validateToken(tokenFromHeader) || validateToken(urlToken))) {
          return reply.code(401).send({
            error: "Unauthorized",
            message: "Invalid or missing token",
          });
        }

        const { text, source_lang, target_lang } = request.body;

        // 执行翻译
        const translatedText = await translate(text, source_lang, target_lang);

        // 构建符合DeepLX格式的响应
        return {
          alternatives: [],
          code: 200,
          data: translatedText,
          id: 1, // Math.floor(Math.random() * 10000000000), // 生成随机ID
          method: "Free",
          source_lang: source_lang.toUpperCase(),
          target_lang: target_lang.toUpperCase(),
        };
      } catch (error) {
        reply.code(500).send({
          code: 500,
          message: error.message,
        });
      }
    }
  );

  fastify.post(
    "/deepl/v1/translate",
    {
      schema: {
        description: "DeepLX v1 API",
        tags: ["plugins"],
        headers: {
          type: "object",
          properties: {
            authorization: {
              type: "string",
              description: "Bearer token",
            },
            "content-type": {
              type: "string",
              description: "内容类型",
            },
          },
        },
        querystring: {
          type: "object",
          properties: {
            token: { type: "string", description: "访问令牌" },
          },
        },
        body: {
          type: "object",
          required: ["text", "source_lang", "target_lang"],
          properties: {
            text: { type: "string", description: "需要翻译的文本" },
            source_lang: { type: "string", description: "源语言代码" },
            target_lang: { type: "string", description: "目标语言代码" },
          },
        },
        response: {
          200: {
            type: "object",
            properties: {
              alternatives: {
                type: "array",
                items: { type: "string" },
                description: "备选翻译结果",
              },
              code: { type: "integer", description: "状态码" },
              data: { type: "string", description: "翻译结果" },
              id: { type: "integer", description: "请求ID" },
              method: { type: "string", description: "翻译方法" },
              source_lang: { type: "string", description: "源语言代码" },
              target_lang: { type: "string", description: "目标语言代码" },
            },
          },
        },
      },
    },
    async (request, reply) => {
      try {
        // 验证Authorization头部或URL参数中的token
        const authHeader = request.headers.authorization;
        const urlToken = request.query.token;

        // 从Bearer token中提取token值
        let tokenFromHeader = null;
        if (authHeader && authHeader.startsWith("Bearer ")) {
          tokenFromHeader = authHeader.substring(7);
        }

        // 检查token是否有效
        if (!(validateToken(tokenFromHeader) || validateToken(urlToken))) {
          return reply.code(401).send({
            error: "Unauthorized",
            message: "Invalid or missing token",
          });
        }

        const { text, source_lang, target_lang } = request.body;

        // 执行翻译
        const translatedText = await translate(text, source_lang, target_lang);

        // 生成随机ID
        // const id = Math.floor(Math.random() * 10000000000);

        // 构建响应
        return {
          alternatives: [], // 实际应用中可能需要提供备选翻译
          code: 200,
          data: translatedText,
          id: 2, // id,
          method: "Pro",
          source_lang: source_lang.toUpperCase(),
          target_lang: target_lang.toUpperCase(),
        };
      } catch (error) {
        reply.code(500).send({
          code: 500,
          message: error.message,
        });
      }
    }
  );
}

module.exports = deeplPlugin;
