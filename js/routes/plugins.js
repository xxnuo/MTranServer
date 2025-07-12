const immePlugin = require('./plugins/imme');
const kissPlugin = require('./plugins/kiss');
const hcfyPlugin = require('./plugins/hcfy');
const googlePlugin = require('./plugins/google');

/**
 * 注册翻译插件兼容路由
 * @param {Object} fastify Fastify实例
 * @param {Object} options 选项
 */
function registerPluginRoutes(fastify, options) {
  // 加载各个插件
  immePlugin(fastify, options);
  kissPlugin(fastify, options);
  hcfyPlugin(fastify, options);
  googlePlugin(fastify, options);
}

module.exports = registerPluginRoutes;
