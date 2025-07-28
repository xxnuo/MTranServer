const { batchTranslate } = require("../../utils/translator");
const { validateToken } = require("../../utils/config");

/**
 * 沉浸式翻译插件API路由处理
 * @param {Object} fastify Fastify实例
 * @param {Object} options 选项
 */
function immePlugin(fastify, options) {
  fastify.post(
    "/imme",
    {
      schema: {
        description: "沉浸式翻译插件API",
        tags: ["plugins"],
        querystring: {
          type: "object",
          // 移除必需的token要求
          properties: {
            token: { type: "string", description: "API访问令牌" },
          },
        },
        body: {
          type: "object",
          properties: {
            source_lang: {
              type: "string",
              description: "源语言代码，默认为auto",
            },
            target_lang: {
              type: "string",
              description: "目标语言代码，默认为zh-Hans",
            },
            text_list: {
              type: "array",
              description: "需要翻译的文本数组",
              items: { type: "string" },
            },
          },
          required: ["text_list"],
        },
        response: {
          200: {
            type: "object",
            properties: {
              translations: {
                type: "array",
                items: {
                  type: "object",
                  properties: {
                    detected_source_lang: {
                      type: "string",
                      description: "检测到的源语言代码",
                    },
                    text: { type: "string", description: "已翻译的文本" },
                  },
                },
              },
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
      // 验证token，直接使用validateToken函数
      const token = request.query.token;
      if (!validateToken(token)) {
        return reply.code(401).send({
          error: "Unauthorized",
          message: "Invalid or missing API token",
        });
      }

      try {
        const { source_lang, target_lang, text_list } = request.body;

        // 检查text_list是否为有效数组
        if (!Array.isArray(text_list) || text_list.length === 0) {
          return reply.code(400).send({
            error: "BadRequest",
            message: "text_list must be a non-empty array",
          });
        }

        // 批量翻译
        const translatedTexts = await batchTranslate(
          text_list,
          source_lang || "auto",
          target_lang || "zh-Hans"
        );

        // 构建符合要求的响应格式
        const translations = translatedTexts.map((text, index) => {
          return {
            detected_source_lang: source_lang || "auto", // 实际应该从翻译服务获取检测到的语言
            text: text,
          };
        });

        return { translations };
      } catch (error) {
        return reply
          .code(500)
          .send({ error: "TranslationError", message: error.message });
      }
    }
  );
}

module.exports = immePlugin;
