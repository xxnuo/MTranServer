"""对已启动的隔离服务验证生成上限恢复；不修改服务或配置。"""
import concurrent.futures
import json
import sys
import time
import urllib.error
import urllib.request

base = sys.argv[1].rstrip('/')
output = sys.argv[2]
records = []
opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def request(name, text, source='zh', target='en', html=False, exact=None):
    start = time.monotonic()
    payload = {'from': source, 'to': target, 'text': text, 'html': html}
    req = urllib.request.Request(base + '/translate', data=json.dumps(payload).encode(), headers={'Content-Type': 'application/json'})
    try:
        with opener.open(req, timeout=120) as response:
            status, data = response.status, json.loads(response.read())
    except urllib.error.HTTPError as error:
        status, data = error.code, json.loads(error.read())
    except Exception as error:
        status, data = 0, {'error': str(error)}
    result = data.get('result')
    ok = status == 200 and isinstance(result, str) and bool(result.strip())
    if exact is not None:
        ok = ok and result == exact
    if name == 'multiline':
        ok = ok and '\r\n--------------------\n' in (result or '') and '\t{0} [1]' in (result or '')
    if html:
        ok = ok and '<p title="original">' in (result or '') and '<b translate="no">保持原文</b>' in (result or '') and '<script>unchanged()</script>' in (result or '')
    record = {'name': name, 'ok': ok, 'status': status, 'seconds': round(time.monotonic()-start, 3), 'input': payload, 'response': data}
    records.append(record)
    print(json.dumps(record, ensure_ascii=False), flush=True)


# 用正常句子初始化，单独验证实际翻译恢复；初始化探测修复由另一个 PR 负责。
request('warmup', '你好，这是一段翻译服务测试。')
request('repeated-hmm', '嗯' * 20)
request('repeated-laughter', '哈哈' * 20)
request('repeated-good', '好' * 40)
request('repeated-words', '好的 ' * 30)
request('repeated-a', 'a ' * 30, 'en', 'zh')
request('separator', '-' * 20, exact='-' * 20)
request('numeric', '1234567890' * 10, exact='1234567890' * 10)
request('punctuation', '。。。。。。', exact='。。。。。。')
request('multiline', '嗯' * 20 + '\r\n--------------------\n你好\t{0} [1]')
request('html', '<p title="original">' + '嗯'*20 + '</p><b translate="no">保持原文</b><script>unchanged()</script>', html=True)
request('long-repeated', '好的 ' * 200)
request('unicode', '你好 e\u0301 🚀 {0} [1]')
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
    list(pool.map(lambda i: request(f'concurrent-{i}', '嗯' * (20+i)), range(4)))
request('normal-after-recovery', '你好，这是一段翻译服务测试。')
print('Waiting for idle timeout...', flush=True)
time.sleep(65)
request('idle-warmup', '你好，这是一段翻译服务测试。')
request('idle-repeated', '嗯' * 20)
summary = {'passed': sum(r['ok'] for r in records), 'total': len(records), 'records': records}
with open(output, 'w', encoding='utf-8') as stream:
    json.dump(summary, stream, ensure_ascii=False, indent=2)
print(json.dumps({'passed': summary['passed'], 'total': summary['total']}), flush=True)
sys.exit(0 if all(r['ok'] for r in records) else 1)
