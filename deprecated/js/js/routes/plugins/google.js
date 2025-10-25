const { translate } = require("../../utils/translator");

/**
 * Google翻译兼容API路由处理
 * @param {Object} fastify Fastify实例
 * @param {Object} options 选项
 */
function googlePlugin(fastify, options) {
  fastify.post(
    "/google/language/translate/v2",
    {
      preHandler: options.authenticate,
      schema: {
        description: "Google翻译兼容API",
        tags: ["plugins"],
        body: {
          type: "object",
          required: ["q", "source", "target"],
          properties: {
            q: { type: "string", description: "需要翻译的文本" },
            source: { type: "string", description: "源语言代码" },
            target: { type: "string", description: "目标语言代码" },
            format: { type: "string", default: "text", description: "文本格式，默认为text" },
          },
        },
        response: {
          200: {
            type: "object",
            properties: {
              data: {
                type: "object",
                properties: {
                  translations: {
                    type: "array",
                    items: {
                      type: "object",
                      properties: {
                        translatedText: { type: "string", description: "翻译结果" }
                      }
                    }
                  }
                }
              }
            }
          },
          500: {
            type: "object",
            properties: {
              error: { type: "string" },
              message: { type: "string" }
            }
          }
        }
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