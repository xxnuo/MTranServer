# MTranServer 高度な設定説明

[中文](../API.md) | [English](API_en.md) | [日本語](API_ja.md) | [Français](API_fr.md) | [Deutsch](API_de.md)

### 環境変数設定

| 環境変数 | 説明 | デフォルト値 | 選択肢 |
| -------- | ---- | ------------ | ------ |
| MT_LOG_LEVEL | ログレベル | warn | debug, info, warn, error |
| MT_CONFIG_DIR | 設定ディレクトリ | ~/.config/mtran/server | 任意のパス |
| MT_MODEL_DIR | モデルディレクトリ | ~/.config/mtran/models | 任意のパス |
| MT_HOST | サーバーリッスンアドレス | 0.0.0.0 | 任意のIPアドレス |
| MT_PORT | サーバーポート | 8989 | 1-65535 |
| MT_ENABLE_UI | Web UI を有効にする | true | true, false |
| MT_OFFLINE | 準備済みの翻訳ファイルでオフライン利用 | false | true, false |
| MT_WORKER_IDLE_TIMEOUT | 待機中の翻訳エンジンを解放するまでの秒数 | 60 | 1, 2, 3, ... |
| MT_API_TOKEN | API アクセストークン | 空 | 任意の文字列 |
| MT_CACHE_SIZE | 保存する翻訳結果の件数；0 で無効 | 1000 | 0, 1, 2, ... |
| MT_MAX_SENTENCE_LENGTH | 長文の分割長 | 512 | 8, 9, 10, ... |
| MT_FULLWIDTH_ZH_PUNCTUATION | 中国語出力のカンマを全角にする | true | true, false |
| MT_LOG_DIR | ログフォルダー | ~/.config/mtran/logs | 任意のパス |
| MT_LOG_TO_FILE | ログをファイルに保存 | false | true, false |
| MT_LOG_CONSOLE | コンソールにログを表示 | true | true, false |
| MT_LOG_REQUESTS | リクエストを記録 | false | true, false |
| MT_CHECK_UPDATE | 起動時に更新を確認 | true | true, false |

ローカルに `models` フォルダーがあれば、初期設定ではそちらを使用します。オフラインにする前に、必要な翻訳方向をオンラインで準備し、設定とモデルの両方のフォルダーを保存してください。

例：

```bash
# ログレベルを debug に設定
export MT_LOG_LEVEL=debug

# ポートを 9000 に設定
export MT_PORT=9000

# サービスを起動
mtranserver
```

### API インターフェース説明

#### システムインターフェース

| インターフェース | メソッド | 説明 | 認証 |
| ---------------- | -------- | ---- | ---- |
| `/version` | GET | サービスバージョンを取得 | いいえ |
| `/health` | GET | ヘルスチェック | いいえ |
| `/__heartbeat__` | GET | ハートビートチェック | いいえ |
| `/__lbheartbeat__` | GET | ロードバランサーハートビートチェック | いいえ |
| `/docs/*` | GET | Swagger API ドキュメント | いいえ |

#### 翻訳インターフェース

| インターフェース | メソッド | 説明 | 認証 |
| ---------------- | -------- | ---- | ---- |
| `/languages` | GET | サポートされている言語リストを取得 | はい |
| `/detect` | POST | 文章の言語を検出 | はい |
| `/translate` | POST | 単一テキスト翻訳 | はい |
| `/translate/batch` | POST | 一括翻訳 | はい |

**単一テキスト翻訳リクエスト例：**

```json
{
  "from": "en",
  "to": "zh-Hans",
  "text": "Hello, world!",
  "html": false
}
```

**一括翻訳リクエスト例：**

```json
{
  "from": "en",
  "to": "zh-Hans",
  "texts": ["Hello, world!", "Good morning!"],
  "html": false
}
```

**認証方式：**

- Header: `Authorization: Bearer <token>`
- Query: `?token=<token>`


詳細については、サーバー起動後の API ドキュメントを参照してください。
