const { translate } = require("../../utils/translator");

/**
 * 划词翻译插件API路由处理
 * @param {Object} fastify Fastify实例
 * @param {Object} options 选项
 */
function hcfyPlugin(fastify, options) {
  fastify.post("/hcfy", async (request, reply) => {
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