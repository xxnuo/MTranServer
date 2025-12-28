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
| MT_OFFLINE | オフラインモード。新しい言語モデルを自動ダウンロードせず、ダウンロード済みのモデルのみ使用 | false | true, false |
| MT_WORKER_IDLE_TIMEOUT | Worker アイドルタイムアウト（秒） | 300 | 任意の正の整数 |
| MT_API_TOKEN | API アクセストークン | 空 | 任意の文字列 |

例：

```bash
# ログレベルを debug に設定
export MT_LOG_LEVEL=debug

# ポートを 9000 に設定
export MT_PORT=9000

# サービスを起動
./mtranserver
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
