const {
  translate,
  batchTranslate,
  supportedLanguages,
} = require("../utils/translator");

/**
 * 注册翻译路由
 * @param {Object} fastify Fastify实例
 * @param {Object} options 选项
 */
function registerTranslateRoutes(fastify, options) {
  // 获取已加载的模型列表
  fastify.get(
    "/languages",
    {
      preHandler: options.authenticate,
      schema: {
        description:
          "Get the supported languages. Use this endpoint to get the language codes for translation.",
        security: [{ Authorization: [] }],
        headers: {
          type: "object",
          properties: {
            Authorization: {
              type: "string",
              description:
                "API token for authentication (format: 'your_token')",
            },
          },
        },
        response: {
          200: {
            type: "object",
            properties: {
              languages: {
                type: "array",
                items: { type: "string" },
                description: "The supported languages",
              },
            },
          },
        },
      },
    },
    async (request, reply) => {
      return { languages: supportedLanguages };
    }
  );

  // 普通翻译API
  fastify.post(
    "/translate",
    {
      preHandler: options.authenticate,
      schema: {
        description:
          "Translate a text. Use this endpoint to translate a single text; from: The source language. Recommend using language code from /languages endpoint. Use 'auto' for automatic detection (adds ~0.04s delay). For Chinese translation, 'zh-Hans' is more efficient than 'zh-CN'; to: The target language. Using language code from /languages endpoint. For Chinese translation, 'zh-Hans' is more efficient than 'zh-CN'; text: The text to translate",
        security: [{ Authorization: [] }],
        headers: {
          type: "object",
          properties: {
            Authorization: {
              type: "string",
              description:
                "API token for authentication (format: 'your_token')",
            },
          },
        },
        body: {
          type: "object",
          required: ["from", "to", "text"],
          properties: {
            from: {
              type: "string",
              description:
                "The source language. Recommend using language code from /languages endpoint. Use 'auto' for automatic detection (adds ~0.04s delay). For Chinese translation, 'zh-Hans' is more efficient than 'zh-CN'",
              default: "auto",
              enum: ["auto", ...supportedLanguages],
            },
            to: {
              type: "string",
              description:
                "The target language. Using language code from /languages endpoint. For Chinese translation, 'zh-Hans' is more efficient than 'zh-CN'",
              default: "zh-Hans",
              enum: supportedLanguages,
            },
            text: {
              type: "string",
              description: "The text to translate",
              default: "Do as you would be done by",
            },
          },
        },
      },
    },
    async (request, reply) => {
      const { from, to, text } = request.body;

      try {
        const result = await translate(text, from, to);
        return { result };
      } catch (error) {
        reply.code(500).send({
          error: "TranslationError",
          message: error.message,
        });
      }
    }
  );

  // 批量翻译API
  fastify.post(
    "/translate/batch",
    {
      preHandler: options.authenticate,
      schema: {
        description:
          "Translate a batch of texts. Use this endpoint to translate a batch of texts. from: The source language. Recommend using language code from /languages endpoint. Use 'auto' for automatic detection (adds ~0.04s delay). For Chinese translation, 'zh-Hans' is more efficient than 'zh-CN'; to: The target language. Using language code from /languages endpoint. For Chinese translation, 'zh-Hans' is more efficient than 'zh-CN'; texts: The texts to translate",
        security: [{ Authorization: [] }],
        headers: {
          type: "object",
          properties: {
            Authorization: {
              type: "string",
              description:
                "API token for authentication (format: 'your_token')",
            },
          },
        },
        body: {
          type: "object",
          required: ["from", "to", "texts"],
          properties: {
            from: {
              type: "string",
              description:
                "The source language. Recommend using language code from /languages endpoint. Use 'auto' for automatic detection (adds ~0.04s delay). For Chinese translation, 'zh-Hans' is more efficient than 'zh-CN'",
              default: "auto",
              enum: ["auto", ...supportedLanguages],
            },
            to: {
              type: "string",
              description:
                "The target language. Using language code from /languages endpoint. For Chinese translation, 'zh-Hans' is more efficient than 'zh-CN'",
              default: "zh-Hans",
              enum: supportedLanguages,
            },
            texts: {
              type: "array",
              items: {
                type: "string",
                default: "Do as you would be done by",
              },
              default: ["Do as you would be done by", "Do unto others"],
              description: "The texts to translate",
            },
          },
        },
      },
    },
    async (request, reply) => {
      const { from, to, texts } = request.body;

      try {
        const results = await batchTranslate(texts, from, to);
        return { results };
      } catch (error) {
        reply.code(500).send({
          error: "BatchTranslationError",
          message: error.message,
        });
      }
    }
  );
}

module.exports = registerTranslateRoutes;
