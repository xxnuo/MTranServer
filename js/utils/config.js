/**
 * 配置管理模块
 */

// 默认配置
const defaultConfig = {
  port: process.env.PORT || 8989,
  host: process.env.HOST || '0.0.0.0',
  apiToken: process.env.CORE_API_TOKEN || '',
  logLevel: process.env.LOG_LEVEL || 'info'
};

/**
 * 获取配置
 * @returns {Object} 配置对象
 */
function getConfig() {
  return { ...defaultConfig };
}

/**
 * 验证API令牌
 * @param {string} token 待验证的令牌
 * @returns {boolean} 是否有效
 */
function validateToken(token) {
  // 如果未设置API令牌，则不需要验证
  if (!defaultConfig.apiToken) {
    return true;
  }
  
  return token === defaultConfig.apiToken;
}

/**
 * 获取服务器版本
 * @returns {string} 版本号
 */
function getVersion() {
  const packageJson = require('../../package.json');
  return packageJson.version || 'unknown';
}

module.exports = {
  getConfig,
  validateToken,
  getVersion
}; 