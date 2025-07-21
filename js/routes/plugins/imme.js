const { translate, batchTranslate } = require("../../utils/translator");

/**
 * 沉浸式翻译插件API路由处理
 * @param {Object} fastify Fastify实例
 * @param {Object} options 选项
 */
function immePlugin(fastify, options) {
  fastify.post("/imme", async (request, reply) => {
    // 验证token
    const token = request.query.token;
    if (!options.validateToken(token)) {
      return reply.code(401).send({
        error: "Unauthorized",
        message: "Invalid or missing API token",
      });
    }

    try {
      const { source_lang, target_lang, text, texts } = request.body;

      // 处理批量翻译
      if (Array.isArray(texts) && texts.length > 0) {
        const results = await batchTranslate(
          texts,
          source_lang || "auto",
          target_lang || "zh-Hans"
        );
        return { data: results };
      }

      // 处理单个文本翻译
      if (text) {
        const result = await translate(
          text,
          source_lang || "auto",
          target_lang || "zh-Hans"
        );
        return { data: result };
      }

      return reply.code(400).send({
        error: "BadRequest",
        message: "Missing text or texts parameter",
      });
    } catch (error) {
      return reply
        .code(500)
        .send({ error: "TranslationError", message: error.message });
    }
  });
}

module.exports = immePlugin;
