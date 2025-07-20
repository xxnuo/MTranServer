const { translate, batchTranslate } = require("../../utils/translator");
const { validateToken } = require("../../utils/config");

/**
 * 简约翻译插件API路由处理
 * @param {Object} fastify Fastify实例
 * @param {Object} options 选项
 */
function kissPlugin(fastify, options) {
  fastify.post(
    "/kiss",
    {
      schema: {
        description: "简约翻译插件API",
        tags: ["plugins"],
        headers: {
          type: "object",
          properties: {
            authorization: { type: "string", description: "Bearer token认证" },
          },
        },
        body: {
          type: "object",
          properties: {
            text: { type: "string", description: "待翻译文字" },
            from: { type: "string", description: "源语言代码，默认为auto" },
            to: { type: "string", description: "目标语言代码，默认为zh-CN" },
            texts: {
              type: "array",
              description: "需要批量翻译的文本数组",
              items: { type: "string" },
            },
          },
          anyOf: [{ required: ["text"] }, { required: ["texts"] }],
        },
        response: {
          200: {
            type: "object",
            properties: {
              text: { type: "string", description: "翻译后的文字" },
              from: { type: "string", description: "识别的源语言" },
              to: { type: "string", description: "目标语言" },
            },
          },
          400: {
            type: "object",
            properties: {
              error: { type: "string" },
              message: { type: "string" },
            },
          },
          401: {
            type: "object",
            properties: {
              error: { type: "string" },
              message: { type: "string" },
            },
          },
          500: {
            type: "object",
            properties: {
              error: { type: "string" },
              message: { type: "string" },
            },
          },
        },
      },
    },
    async (request, reply) => {
      // 从Authorization头部提取token
      const authHeader = request.headers.authorization || "";
      const token = authHeader.startsWith("Bearer ")
        ? authHeader.substring(7)
        : null;

      if (!validateToken(token)) {
        return reply.code(401).send({
          error: "Unauthorized",
          message: "Invalid or missing API token",
        });
      }

      try {
        const { from, to, text, texts } = request.body;
        const targetLang = to || "zh-CN";
        const sourceLang = from || "auto";

        // 处理批量翻译
        if (Array.isArray(texts) && texts.length > 0) {
          const results = await batchTranslate(texts, sourceLang, targetLang);
          return {
            text: results.join("\n"),
            from: sourceLang,
            to: targetLang,
          };
        }

        // 处理单个文本翻译
        if (text) {
          const result = await translate(text, sourceLang, targetLang);
          return {
            text: result,
            from: sourceLang,
            to: targetLang,
          };
        }

        return reply.code(400).send({
          error: "BadRequest",
          message: "Missing text or texts parameter",
        });
      } catch (error) {
        return reply.code(500).send({
          error: "TranslationError",
          message: error.message,
        });
      }
    }
  );
}

module.exports = kissPlugin;
