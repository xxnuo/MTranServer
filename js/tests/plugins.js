#!/usr/bin/env node

/**
 * 测试翻译插件兼容API
 * 包括沉浸式翻译、简约翻译和划词翻译API
 */

const http = require('http');

// 配置
const config = {
  host: process.env.HOST || 'localhost',
  port: process.env.PORT || 8989,
  token: process.env.CORE_API_TOKEN || ''
};

// 测试沉浸式翻译API
function testImmeTranslate() {
  console.log('测试沉浸式翻译API...');
  const tokenParam = config.token ? `?token=${config.token}` : '';
  const options = {
    hostname: config.host,
    port: config.port,
    path: `/imme${tokenParam}`,
    method: 'POST',
    headers: {
      'Content-Type': 'application/json'
    }
  };
  
  const data = JSON.stringify({
    source_lang: 'en',
    target_lang: 'zh-Hans',
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
      console.log('沉浸式翻译API测试完成\n');
      
      // 测试批量翻译
      testImmeBatchTranslate();
    });
  });

  req.on('error', (error) => {
    console.error(`沉浸式翻译API测试出错: ${error.message}`);
  });

  req.write(data);
  req.end();
}

// 测试沉浸式翻译批量API
function testImmeBatchTranslate() {
  console.log('测试沉浸式翻译批量API...');
  const tokenParam = config.token ? `?token=${config.token}` : '';
  const options = {
    hostname: config.host,
    port: config.port,
    path: `/imme${tokenParam}`,
    method: 'POST',
    headers: {
      'Content-Type': 'application/json'
    }
  };
  
  const data = JSON.stringify({
    source_lang: 'en',
    target_lang: 'zh-Hans',
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
      console.log('沉浸式翻译批量API测试完成\n');
      
      // 测试下一个API
      testKissTranslate();
    });
  });

  req.on('error', (error) => {
    console.error(`沉浸式翻译批量API测试出错: ${error.message}`);
  });

  req.write(data);
  req.end();
}

// 测试简约翻译API
function testKissTranslate() {
  console.log('测试简约翻译API...');
  const options = {
    hostname: config.host,
    port: config.port,
    path: '/kiss',
    method: 'POST',
    headers: {
      'Content-Type': 'application/json'
    }
  };
  
  // 如果有token，添加到请求头
  if (config.token) {
    options.headers['Key'] = config.token;
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
      console.log('简约翻译API测试完成\n');
      
      // 测试批量翻译
      testKissBatchTranslate();
    });
  });

  req.on('error', (error) => {
    console.error(`简约翻译API测试出错: ${error.message}`);
  });

  req.write(data);
  req.end();
}

// 测试简约翻译批量API
function testKissBatchTranslate() {
  console.log('测试简约翻译批量API...');
  const options = {
    hostname: config.host,
    port: config.port,
    path: '/kiss',
    method: 'POST',
    headers: {
      'Content-Type': 'application/json'
    }
  };
  
  // 如果有token，添加到请求头
  if (config.token) {
    options.headers['Key'] = config.token;
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
      console.log('简约翻译批量API测试完成\n');
      
      // 测试下一个API
      testHcfyTranslate();
    });
  });

  req.on('error', (error) => {
    console.error(`简约翻译批量API测试出错: ${error.message}`);
  });

  req.write(data);
  req.end();
}

// 测试划词翻译API
function testHcfyTranslate() {
  console.log('测试划词翻译API...');
  const tokenParam = config.token ? `?token=${config.token}` : '';
  const options = {
    hostname: config.host,
    port: config.port,
    path: `/hcfy${tokenParam}`,
    method: 'POST',
    headers: {
      'Content-Type': 'application/json'
    }
  };
  
  const data = JSON.stringify({
    text: 'Hello, world!',
    from: 'en',
    to: 'zh-Hans'
  });

  const req = http.request(options, (res) => {
    let responseData = '';
    res.on('data', (chunk) => {
      responseData += chunk;
    });
    res.on('end', () => {
      console.log(`状态码: ${res.statusCode}`);
      console.log(`响应数据: ${responseData}`);
      console.log('划词翻译API测试完成\n');
      
      console.log('所有翻译插件兼容API测试完成');
    });
  });

  req.on('error', (error) => {
    console.error(`划词翻译API测试出错: ${error.message}`);
  });

  req.write(data);
  req.end();
}

// 开始测试
console.log('开始测试翻译插件兼容API...\n');
testImmeTranslate(); 