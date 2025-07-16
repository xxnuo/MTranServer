const { getVersion } = require("../utils/config");

/**
 * 注册核心路由
 * @param {Object} fastify Fastify实例
 * @param {Object} options 选项
 */
function registerBaseRoutes(fastify, options) {
  // 版本信息
  fastify.get(
    "/version",
    {
      schema: {
        description: "Get the version of the server",
        response: {
          200: {
            type: "object",
            properties: {
              version: { type: "string" },
            },
          },
        },
      },
    },
    async (request, reply) => {
      return { version: getVersion() };
    }
  );

  // 健康检查
  fastify.get(
    "/health",
    {
      schema: {
        description: "Get the health of the server",
        response: {
          200: {
            type: "object",
            properties: {
              status: { type: "string" },
            },
          },
        },
      },
    },
    async (request, reply) => {
      return { status: "ok" };
    }
  );

  // 心跳检查
  fastify.get(
    "/__heartbeat__",
    {
      schema: {
        description: "Get the heartbeat of the server",
        response: {
          200: {
            type: "object",
            properties: {
              status: { type: "string" },
            },
          },
        },
      },
    },
    async (request, reply) => {
      return { status: "ok" };
    }
  );

  // 负载均衡心跳检查
  fastify.get(
    "/__lbheartbeat__",
    {
      schema: {
        description: "Get the heartbeat of the server",
        response: {
          200: {
            type: "object",
            properties: {
              status: { type: "string" },
            },
          },
        },
      },
    },
    async (request, reply) => {
      return { status: "ok" };
    }
  );
}

module.exports = registerBaseRoutes;
