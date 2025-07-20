#!/usr/bin/env node

/**
 * 测试核心API
 * 包括版本、健康检查、心跳检查和模型列表API
 */

const http = require('http');

// 配置
const config = {
  host: process.env.HOST || 'localhost',
  port: process.env.PORT || 8989,
  token: process.env.CORE_API_TOKEN || ''
};

// 测试版本API
function testVersion() {
  console.log('测试版本API...');
  const options = {
    hostname: config.host,
    port: config.port,
    path: '/version',
    method: 'GET'
  };

  const req = http.request(options, (res) => {
    let data = '';
    res.on('data', (chunk) => {
      data += chunk;
    });
    res.on('end', () => {
      console.log(`状态码: ${res.statusCode}`);
      console.log(`响应数据: ${data}`);
      console.log('版本API测试完成\n');
      
      // 测试下一个API
      testHealth();
    });
  });

  req.on('error', (error) => {
    console.error(`版本API测试出错: ${error.message}`);
  });

  req.end();
}

// 测试健康检查API
function testHealth() {
  console.log('测试健康检查API...');
  const options = {
    hostname: config.host,
    port: config.port,
    path: '/health',
    method: 'GET'
  };

  const req = http.request(options, (res) => {
    let data = '';
    res.on('data', (chunk) => {
      data += chunk;
    });
    res.on('end', () => {
      console.log(`状态码: ${res.statusCode}`);
      console.log(`响应数据: ${data}`);
      console.log('健康检查API测试完成\n');
      
      // 测试下一个API
      testHeartbeat();
    });
  });

  req.on('error', (error) => {
    console.error(`健康检查API测试出错: ${error.message}`);
  });

  req.end();
}

// 测试心跳检查API
function testHeartbeat() {
  console.log('测试心跳检查API...');
  const options = {
    hostname: config.host,
    port: config.port,
    path: '/__heartbeat__',
    method: 'GET'
  };

  const req = http.request(options, (res) => {
    let data = '';
    res.on('data', (chunk) => {
      data += chunk;
    });
    res.on('end', () => {
      console.log(`状态码: ${res.statusCode}`);
      console.log(`响应数据: ${data}`);
      console.log('心跳检查API测试完成\n');
      
      // 测试下一个API
      testLBHeartbeat();
    });
  });

  req.on('error', (error) => {
    console.error(`心跳检查API测试出错: ${error.message}`);
  });

  req.end();
}

// 测试负载均衡心跳检查API
function testLBHeartbeat() {
  console.log('测试负载均衡心跳检查API...');
  const options = {
    hostname: config.host,
    port: config.port,
    path: '/__lbheartbeat__',
    method: 'GET'
  };

  const req = http.request(options, (res) => {
    let data = '';
    res.on('data', (chunk) => {
      data += chunk;
    });
    res.on('end', () => {
      console.log(`状态码: ${res.statusCode}`);
      console.log(`响应数据: ${data}`);
      console.log('负载均衡心跳检查API测试完成\n');
      
      // 测试下一个API
      testModels();
    });
  });

  req.on('error', (error) => {
    console.error(`负载均衡心跳检查API测试出错: ${error.message}`);
  });

  req.end();
}

// 测试模型列表API
function testModels() {
  console.log('测试模型列表API...');
  const options = {
    hostname: config.host,
    port: config.port,
    path: '/models',
    method: 'GET',
    headers: {}
  };
  
  // 如果有token，添加到请求头
  if (config.token) {
    options.headers['Authorization'] = config.token;
  }

  const req = http.request(options, (res) => {
    let data = '';
    res.on('data', (chunk) => {
      data += chunk;
    });
    res.on('end', () => {
      console.log(`状态码: ${res.statusCode}`);
      console.log(`响应数据: ${data}`);
      console.log('模型列表API测试完成\n');
      
      console.log('所有核心API测试完成');
    });
  });

  req.on('error', (error) => {
    console.error(`模型列表API测试出错: ${error.message}`);
  });

  req.end();
}

// 开始测试
console.log('开始测试核心API...\n');
testVersion(); 