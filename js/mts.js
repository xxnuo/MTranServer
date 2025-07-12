#!/usr/bin/env node

const fastify = require('fastify');
const cors = require('@fastify/cors');
const { getConfig, validateToken } = require('./utils/config');
const { authenticate, errorHandler } = require('./utils/middleware');
const { getSupportedLanguages, preloadModel, shutdown } = require('./utils/translator');
const registerCoreRoutes = require('./routes/core');
const registerTranslateRoutes = require('./routes/translate');
const registerPluginRoutes = require('./routes/plugins');

// 获取配置
const config = getConfig();

// 创建Fastify实例
const server = fastify({
  logger: {
    level: config.logLevel,
    transport: {
      target: 'pino-pretty',
      options: {
        translateTime: 'HH:MM:ss Z',
        ignore: 'pid,hostname'
      }
    }
  }
});

// 注册CORS中间件
server.register(cors, {
  origin: true,
  methods: ['GET', 'POST', 'OPTIONS'],
  allowedHeaders: ['Content-Type', 'Authorization', 'Key']
});

// 设置错误处理器
server.setErrorHandler(errorHandler);

// 路由选项
const routeOptions = {
  authenticate: authenticate,
  validateToken: validateToken
};

// 注册路由
server.register(registerCoreRoutes, routeOptions);
server.register(registerTranslateRoutes, routeOptions);
server.register(registerPluginRoutes, routeOptions);

// 启动服务器
async function start() {
  try {
    // 输出服务信息
    console.log(`Service port: ${config.port}`);
    
    // 获取支持的语言列表
    const languages = getSupportedLanguages();
    
    // 预加载常用语言对
    // await preloadModel('en', 'zh-Hans');

    // 监听端口
    await server.listen({ port: config.port, host: config.host });
  } catch (err) {
    server.log.error(err);
    process.exit(1);
  }
}

// 处理进程退出
process.on('SIGINT', async () => {
  console.log('Shutting down server...');
  await shutdown();
  await server.close();
  process.exit(0);
});

process.on('SIGTERM', async () => {
  console.log('Shutting down server...');
  await shutdown();
  await server.close();
  process.exit(0);
});

// 启动服务器
start();
