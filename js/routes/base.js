const { getVersion } = require("../utils/config");

/**
 * 注册核心路由
 * @param {Object} fastify Fastify实例
 * @param {Object} options 选项
 */
function registerBaseRoutes(fastify, options) {
  // 版本信息
  fastify.get("/version", async (request, reply) => {
    return { version: getVersion() };
  });

  // 健康检查
  fastify.get("/health", async (request, reply) => {
    return { status: "ok" };
  });

  // 心跳检查
  fastify.get("/__heartbeat__", async (request, reply) => {
    reply.type("text/plain");
    return "Ready";
  });

  // 负载均衡心跳检查
  fastify.get("/__lbheartbeat__", async (request, reply) => {
    reply.type("text/plain");
    return "Ready";
  });
}

module.exports = registerBaseRoutes;
