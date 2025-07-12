#!/usr/bin/env node

/**
 * 测试翻译API
 * 包括普通翻译、批量翻译和Google翻译兼容API
 */

const http = require('http');

// 配置
const config = {
  host: process.env.HOST || 'localhost',
  port: process.env.PORT || 8989,
  token: process.env.CORE_API_TOKEN || ''
};

// 测试普通翻译API
function testTranslate() {
  console.log('测试普通翻译API...');
  const options = {
    hostname: config.host,
    port: config.port,
    path: '/translate',
    method: 'POST',
    headers: {
      'Content-Type': 'application/json'
    }
  };
  
  // 如果有token，添加到请求头
  if (config.token) {
    options.headers['Authorization'] = config.token;
  }
  
  const data = JSON.stringify({
    from: 'en',
    to: 'zh-Hans',
    text: 'Hello, world!'
  });

  const req = http.request(options, (res) => {
    let responseData = '';
    res.on('data', (chunk) => {
      responseData += chunk;
    });
    res.on('end', () => {
      console.log(`状态码: ${res.statusCode}`);
      console.log(`响应数据: ${responseData}`);
      console.log('普通翻译API测试完成\n');
      
      // 测试下一个API
      testBatchTranslate();
    });
  });

  req.on('error', (error) => {
    console.error(`普通翻译API测试出错: ${error.message}`);
  });

  req.write(data);
  req.end();
}

// 测试批量翻译API
function testBatchTranslate() {
  console.log('测试批量翻译API...');
  const options = {
    hostname: config.host,
    port: config.port,
    path: '/translate/batch',
    method: 'POST',
    headers: {
      'Content-Type': 'application/json'
    }
  };
  
  // 如果有token，添加到请求头
  if (config.token) {
    options.headers['Authorization'] = config.token;
  }
  
  const data = JSON.stringify({
    from: 'en',
    to: 'zh-Hans',
    texts: ['Hello, world!', 'How are you?']
  });

  const req = http.request(options, (res) => {
    let responseData = '';
    res.on('data', (chunk) => {
      responseData += chunk;
    });
    res.on('end', () => {
      console.log(`状态码: ${res.statusCode}`);
      console.log(`响应数据: ${responseData}`);
      console.log('批量翻译API测试完成\n');
      
      // 测试下一个API
      testGoogleTranslate();
    });
  });

  req.on('error', (error) => {
    console.error(`批量翻译API测试出错: ${error.message}`);
  });

  req.write(data);
  req.end();
}

// 测试Google翻译兼容API
function testGoogleTranslate() {
  console.log('测试Google翻译兼容API...');
  const options = {
    hostname: config.host,
    port: config.port,
    path: '/language/translate/v2',
    method: 'POST',
    headers: {
      'Content-Type': 'application/json'
    }
  };
  
  // 如果有token，添加到请求头
  if (config.token) {
    options.headers['Authorization'] = config.token;
  }
  
  const data = JSON.stringify({
    q: 'The Great Pyramid of Giza',
    source: 'en',
    target: 'zh-Hans',
    format: 'text'
  });

  const req = http.request(options, (res) => {
    let responseData = '';
    res.on('data', (chunk) => {
      responseData += chunk;
    });
    res.on('end', () => {
      console.log(`状态码: ${res.statusCode}`);
      console.log(`响应数据: ${responseData}`);
      console.log('Google翻译兼容API测试完成\n');
      
      console.log('所有翻译API测试完成');
    });
  });

  req.on('error', (error) => {
    console.error(`Google翻译兼容API测试出错: ${error.message}`);
  });

  req.write(data);
  req.end();
}

// 开始测试
console.log('开始测试翻译API...\n');
testTranslate(); 