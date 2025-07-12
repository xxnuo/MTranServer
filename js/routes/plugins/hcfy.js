const { translate } = require("../../utils/translator");

/**
 * 划词翻译插件API路由处理
 * @param {Object} fastify Fastify实例
 * @param {Object} options 选项
 */
function hcfyPlugin(fastify, options) {
  fastify.post("/hcfy", {
    schema: {
      description: "划词翻译插件API",
      tags: ["plugins"],
      querystring: {
        type: "object",
        required: ["token"],
        properties: {
          token: { type: "string", description: "API访问令牌" }
        }
      },
      body: {
        type: "object",
        required: ["text"],
        properties: {
          text: { type: "string", description: "需要翻译的文本" },
          from: { type: "string", description: "源语言代码，默认为auto" },
          to: { type: "string", description: "目标语言代码，默认为zh-Hans" }
        }
      },
      response: {
        200: {
          type: "object",
          properties: {
            translation: { type: "string", description: "翻译结果" }
          }
        },
        400: {
          type: "object",
          properties: {
            error: { type: "string" },
            message: { type: "string" }
          }
        },
        401: {
          type: "object",
          properties: {
            error: { type: "string" },
            message: { type: "string" }
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
    }
  }, async (request, reply) => {
    // 验证token
    const token = request.query.token;
    if (!options.validateToken(token)) {
      return reply.code(401).send({
        error: "Unauthorized",
        message: "Invalid or missing API token",
      });
    }

    try {
      const { text, from, to } = request.body;

      if (!text) {
        return reply
          .code(400)
          .send({ error: "BadRequest", message: "Missing text parameter" });
      }

      const result = await translate(text, from || "auto", to || "zh-Hans");
      return { translation: result };
    } catch (error) {
      return reply
        .code(500)
        .send({ error: "TranslationError", message: error.message });
    }
  });
}

module.exports = hcfyPlugin; 