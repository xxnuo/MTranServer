const { validateToken } = require('./config');

/**
 * 认证中间件
 * @param {Object} request Fastify请求对象
 * @param {Object} reply Fastify响应对象
 * @param {Function} done 完成回调
 */
function authenticate(request, reply, done) {
  const authHeader = request.headers.authorization;
  const token = request.query.token;
  
  // 检查Authorization头或URL中的token参数
  if (validateToken(authHeader) || validateToken(token)) {
    done();
  } else {
    reply.code(401).send({ error: 'Unauthorized', message: 'Invalid or missing API token' });
  }
}

/**
 * 错误处理中间件
 * @param {Error} error 错误对象
 * @param {Object} request Fastify请求对象
 * @param {Object} reply Fastify响应对象
 */
function errorHandler(error, request, reply) {
  console.error(`Error processing request: ${error.message}`);
  
  // 返回适当的错误响应
  reply.code(error.statusCode || 500).send({
    error: error.name || 'InternalServerError',
    message: error.message || 'An unknown error occurred'
  });
}

module.exports = {
  authenticate,
  errorHandler
}; 