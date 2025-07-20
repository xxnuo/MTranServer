const {
  translate,
  batchTranslate,
  getSupportedLanguages,
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
    },
    async (request, reply) => {
      return { languages: getSupportedLanguages() };
    }
  );

  // 普通翻译API
  fastify.post(
    "/translate",
    {
      preHandler: options.authenticate,
      schema: {
        body: {
          type: "object",
          required: ["from", "to", "text"],
          properties: {
            from: { type: "string" },
            to: { type: "string" },
            text: { type: "string" },
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
        body: {
          type: "object",
          required: ["from", "to", "texts"],
          properties: {
            from: { type: "string" },
            to: { type: "string" },
            texts: {
              type: "array",
              items: { type: "string" },
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
