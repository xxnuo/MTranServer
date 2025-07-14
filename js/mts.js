#!/usr/bin/env node

const fastify = require("fastify");
const cors = require("@fastify/cors");
const swagger = require("@fastify/swagger");
const swaggerUi = require("@fastify/swagger-ui");
const { getConfig, validateToken } = require("./utils/config");
const { authenticate, errorHandler } = require("./utils/middleware");
const { shutdown } = require("./utils/translator");
const registerTranslateRoutes = require("./routes/translate");
const registerPluginRoutes = require("./routes/plugins");
const registerBaseRoutes = require("./routes/base");

// 获取配置
const config = getConfig();

// 创建Fastify实例
const server = fastify({
  logger: {
    level: config.logLevel,
    transport: {
      target: "pino-pretty",
      options: {
        translateTime: "HH:MM:ss Z",
        ignore: "pid,hostname",
      },
    },
  },
  trustProxy: true, // 信任代理，允许获取正确的客户端 IP 和协议
});

// 注册CORS中间件
server.register(cors, {
  origin: true,
  methods: ["GET", "POST", "OPTIONS"],
  allowedHeaders: ["Content-Type", "Authorization", "Key"],
});

// 注册Swagger生成器
server.register(swagger, {
  openapi: {
    info: {
      title: "MTranServer API",
      description: "MTranServer API documentation",
      version: config.version || "3.0.0",
    },
    components: {
      securitySchemes: {
        apiKey: {
          type: "apiKey",
          name: "Key",
          in: "header",
        },
      },
    },
  },
});

// 注册Swagger UI
server.register(swaggerUi, {
  routePrefix: "/docs",
  uiConfig: {
    docExpansion: "list",
    deepLinking: false,
    tryItOutEnabled: false,
    persistAuthorization: true,
    displayRequestDuration: true,
    syntaxHighlight: {
      activate: true,
      theme: "agate",
    },
  },
  transformSpecification: (swaggerObject, request, reply) => {
    // 动态调整 URL 协议，确保与请求协议一致
    const protocol = request.protocol || "http";
    swaggerObject.servers = [
      {
        url: `${protocol}://${config.host}:${config.port}`,
        description: `MTranServer API (${protocol.toUpperCase()})`,
      },
    ];
    return swaggerObject;
  },
  uiHooks: {
    onRequest: function (request, reply, next) {
      next();
    },
    preHandler: function (request, reply, next) {
      next();
    },
  },
  staticCSP: false, // 禁用静态CSP以避免混合内容问题
  transformStaticCSP: (header) => header,
});

// 设置错误处理器
server.setErrorHandler(errorHandler);

// 路由选项
const routeOptions = {
  authenticate: authenticate,
  validateToken: validateToken,
};

// 注册路由
server.register(registerBaseRoutes, routeOptions);
server.register(registerTranslateRoutes, routeOptions);
server.register(registerPluginRoutes, routeOptions);

// 启动服务器
async function start() {
  try {
    // 配置 HTTPS 选项
    const listenOptions = {
      port: config.port,
      host: config.host,
    };

    // 如果配置了 HTTPS，则添加 HTTPS 选项
    if (config.https && config.httpsKey && config.httpsCert) {
      const fs = require("fs");
      listenOptions.https = {
        key: fs.readFileSync(config.httpsKey),
        cert: fs.readFileSync(config.httpsCert),
      };
      console.log(`HTTPS Service URL: https://${config.host}:${config.port}`);
    } else {
      // 输出服务信息
      console.log(`HTTP Service URL: http://${config.host}:${config.port}`);
    }

    // 监听端口
    await server.listen(listenOptions);
  } catch (err) {
    server.log.error(err);
    process.exit(1);
  }
}

// 处理进程退出
process.on("SIGINT", async () => {
  console.log("Shutting down server...");
  try {
    await shutdown();
    await server.close();
    console.log("Server shutdown complete");
    process.exit(0);
  } catch (err) {
    console.error("Error during shutdown:", err);
    process.exit(1);
  }
});

process.on("SIGTERM", async () => {
  console.log("Shutting down server...");
  try {
    await shutdown();
    await server.close();
    console.log("Server shutdown complete");
    process.exit(0);
  } catch (err) {
    console.error("Error during shutdown:", err);
    process.exit(1);
  }
});

// 启动服务器
start();
