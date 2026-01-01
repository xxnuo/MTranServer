import { describe, test, expect } from 'bun:test';
import { detectLanguage } from '@/services/detector';

describe('CLD2 Memory Safety Tests', () => {
  test('包含 null 字节的字符串', async () => {
    const text = 'Hello\0World';
    const result = await detectLanguage(text);
    expect(result).toBeDefined();
    expect(typeof result).toBe('string');
  });

  test('超长文本（1MB）', async () => {
    const text = 'A'.repeat(1024 * 1024);
    const result = await detectLanguage(text);
    expect(result).toBeDefined();
  });

  test('混合 UTF-8 多字节字符', async () => {
    const text = '你好世界🌍Hello'.repeat(1000);
    const result = await detectLanguage(text);
    expect(result).toBeDefined();
  });

  test('控制字符', async () => {
    const text = 'Test\x01\x02\x03Text';
    const result = await detectLanguage(text);
    expect(result).toBeDefined();
  });

  test('连续多次检测不崩溃', async () => {
    for (let i = 0; i < 100; i++) {
      const text = `Test ${i} with special chars 你好\0\x01`;
      await detectLanguage(text);
    }
  });

  test('空文本', async () => {
    const result = await detectLanguage('');
    expect(result).toBe('');
  });

  test('纯空白字符', async () => {
    const text = '   \n\t  ';
    const result = await detectLanguage(text);
    expect(result).toBeDefined();
  });

  test('emoji 表情符号', async () => {
    const text = '🎉🎊🎈🎁🎀';
    const result = await detectLanguage(text);
    expect(result).toBeDefined();
  });
});
