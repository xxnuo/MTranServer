const { translate, batchTranslate } = require("../../utils/translator");

/**
 * 简约翻译插件API路由处理
 * @param {Object} fastify Fastify实例
 * @param {Object} options 选项
 */
function kissPlugin(fastify, options) {
  fastify.post("/kiss", async (request, reply) => {
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