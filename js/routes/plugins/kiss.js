const { translate, batchTranslate } = require("../../utils/translator");

/**
 * 简约翻译插件API路由处理
 * @param {Object} fastify Fastify实例
 * @param {Object} options 选项
 */
function kissPlugin(fastify, options) {
  fastify.post("/kiss", {
    schema: {
      description: "简约翻译插件API",
      tags: ["plugins"],
      headers: {
        type: "object",
        required: ["key"],
        properties: {
          key: { type: "string", description: "API访问令牌" }
        }
      },
      body: {
        type: "object",
        properties: {
          from: { type: "string", description: "源语言代码，默认为auto" },
          to: { type: "string", description: "目标语言代码，默认为zh-Hans" },
          text: { type: "string", description: "需要翻译的文本" },
          texts: { 
            type: "array", 
            description: "需要批量翻译的文本数组",
            items: { type: "string" }
          }
        },
        anyOf: [
          { required: ["text"] },
          { required: ["texts"] }
        ]
      },
      response: {
        200: {
          type: "object",
          properties: {
            code: { type: "integer", description: "状态码" },
            text: { type: "string", description: "翻译结果，批量翻译时为换行符分隔的结果" }
          }
        },
        400: {
          type: "object",
          properties: {
            code: { type: "integer" },
            error: { type: "string" }
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
            code: { type: "integer" },
            error: { type: "string" }
          }
        }
      }
    }
  }, async (request, reply) => {
    // 验证token
    const token = request.headers.key;
    if (!options.validateToken(token)) {
      return reply.code(401).send({
        error: "Unauthorized",
        message: "Invalid or missing API token",
      });
    }

    try {
      const { from, to, text, texts } = request.body;

      // 处理批量翻译
      if (Array.isArray(texts) && texts.length > 0) {
        const results = await batchTranslate(
          texts,
          from || "auto",
          to || "zh-Hans"
        );
        return { code: 200, text: results.join("\n") };
      }

      // 处理单个文本翻译
      if (text) {
        const result = await translate(text, from || "auto", to || "zh-Hans");
        return { code: 200, text: result };
      }

      return reply
        .code(400)
        .send({ code: 400, error: "Missing text or texts parameter" });
    } catch (error) {
      return reply.code(500).send({ code: 500, error: error.message });
    }
  });
}

module.exports = kissPlugin; 