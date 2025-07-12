const { translate } = require("../../utils/translator");

/**
 * Google翻译兼容API路由处理
 * @param {Object} fastify Fastify实例
 * @param {Object} options 选项
 */
function googlePlugin(fastify, options) {
  fastify.post(
    "/language/translate/v2",
    {
      preHandler: options.authenticate,
      schema: {
        body: {
          type: "object",
          required: ["q", "source", "target"],
          properties: {
            q: { type: "string" },
            source: { type: "string" },
            target: { type: "string" },
            format: { type: "string", default: "text" },
          },
        },
      },
    },
    async (request, reply) => {
      const { q, source, target } = request.body;

      try {
        const translatedText = await translate(q, source, target);
        return {
          data: {
            translations: [{ translatedText }],
          },
        };
      } catch (error) {
        reply.code(500).send({
          error: "TranslationError",
          message: error.message,
        });
      }
    }
  );
}

module.exports = googlePlugin; 