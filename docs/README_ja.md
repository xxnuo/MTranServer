# MTranServer

[中文](../README.md) | [English](README_en.md) | [日本語](README_ja.md) | [Français](README_fr.md) | [Deutsch](README_de.md)

<!-- <img src="../images/icon.png" width="64px" height="64px" align="right" alt="MTran"> -->

超低リソース消費、超高速なオフライン翻訳モデルサーバーです。グラフィックカードは不要です。リクエストあたりの平均応答時間は50ミリ秒です。世界の主要言語の翻訳をサポートしています。

注意：このモデルサーバーは、`オフライン翻訳`、`応答速度`、`クロスプラットフォーム展開`、`ローカル実行`による`無制限の無料翻訳`という設計目標に焦点を当てており、モデルサイズと最適化の制限により、翻訳品質は大モデル翻訳の効果には及びません。高品質な翻訳が必要な場合は、オンラインの大規模言語モデルAPIの使用をお勧めします。

> v4ではメモリ使用量が最適化され、速度がさらに向上し、安定性が強化されました。古いバージョンを使用している場合は、すぐにアップグレードすることをお勧めします！

<img src="../images/preview.png" width="auto" height="460">

## オンラインデモ

| ウェブサイト                     | TOKEN                     | その他のインターフェース                                                                       | 提供者  |
| -------------------------------- | ------------------------- | ---------------------------------------------------------------------------------------------- | ------- |
| `https://MTranServer.ipacel.cc/` | `__IpacEL_MT_API_TOKEN__` | 没入型翻訳: `https://MTranServer.ipacel.cc/imme?token=__IpacEL_MT_API_TOKEN__` | @ApliNi |

ユーザーに試用サービスを提供してくださるコミュニティの貢献者に感謝します！

## 使用方法

[Releases](https://github.com/xxnuo/MTranServer/releases) から対応するプラットフォームの最新バージョンをダウンロードし、コマンドラインでプログラムを起動するだけで使用できます。

[MTranServer](https://github.com/xxnuo/MTranServer) は主にサーバー使用環境向けであるため、現在はコマンドラインサービスと Docker デプロイのみ提供しています。

デスクトップアプリケーションとブラウザ拡張機能を提供する予定ですので、ご期待ください。

> **重要：**
>
> 初めて特定の言語ペアを翻訳する場合、サーバーは対応する翻訳モデルを自動的にダウンロードします（オフラインモードが有効でない場合）。このプロセスには、ネットワーク速度とモデルサイズに応じて時間がかかる場合があります。モデルのダウンロード完了後、その後の翻訳リクエストはミリ秒レベルの応答速度になります。正式に使用する前に一度翻訳をテストし、サーバーにモデルを事前にダウンロードしてロードさせることをお勧めします。
>
> プログラムは頻繁に更新されます。問題が発生した場合は、最新バージョンに更新してみてください。

サーバー起動後、ログにプログラム付属の簡易 UI のアドレスとオンラインドキュメントのアドレスが出力されます。以下はプレビューです。

![UI](../images/ui.png)

![ドキュメント](../images/swagger.png)


### コマンドライン引数

```bash
./mtranserver [オプション]

オプション：
  -version, -v          バージョン情報を表示
  -log-level string     ログレベル (debug, info, warn, error) (デフォルト "warn")
  -config-dir string    設定ディレクトリ (デフォルト "~/.config/mtran/server")
  -model-dir string     モデルディレクトリ (デフォルト "~/.config/mtran/models")
  -host string          サーバーリッスンアドレス (デフォルト "0.0.0.0")
  -port string          サーバーポート (デフォルト "8989")
  -ui                   Web UI を有効にする (デフォルト true)
  -offline              オフラインモードを有効にする（新しいモデルを自動ダウンロードしない） (デフォルト false)
  -worker-idle-timeout int  Worker アイドルタイムアウト（秒） (デフォルト 300)

例：
  ./mtranserver --host 127.0.0.1 --port 8080
  ./mtranserver --ui --offline
  ./mtranserver -v
```

### Docker Compose デプロイ

空のディレクトリを作成し、以下の内容で `compose.yml` ファイルを作成します。

```yml
services:
  mtranserver:
    image: xxnuo/mtranserver:latest
    container_name: mtranserver
    restart: unless-stopped
    ports:
      - "8989:8989"
    environment:
      - MT_HOST=0.0.0.0
      - MT_PORT=8989
      - MT_ENABLE_UI=true
      - MT_OFFLINE=false
      # - MT_API_TOKEN=your_secret_token_here
    volumes:
      - ./models:/app/models
```

```bash
docker pull xxnuo/mtranserver:latest
docker compose up -d
```

#### 翻訳プラグイン互換インターフェース

サーバーは複数の翻訳プラグインの互換インターフェースを提供しています。

| インターフェース | メソッド | 説明 | 対応プラグイン |
| ---------------- | -------- | ---- | -------------- |
| `/imme` | POST | 没入型翻訳（Immersive Translate）プラグインインターフェース | [没入型翻訳](https://immersivetranslate.com/) |
| `/kiss` | POST | Kiss Translator プラグインインターフェース | [Kiss Translator](https://github.com/fishjar/kiss-translator) |
| `/deepl` | POST | DeepL API v2 互換インターフェース | DeepL API 対応クライアント |
| `/google/language/translate/v2` | POST | Google Translate API v2 互換インターフェース | Google Translate API 対応クライアント |
| `/google/translate_a/single` | GET | Google translate_a/single 互換インターフェース | Google ウェブ翻訳対応クライアント |
| `/hcfy` | POST | 划词翻译（Selection Translator）互換インターフェース | [划词翻译](https://github.com/Selection-Translator/crx-selection-translate) |

**プラグイン設定説明：**

> 注：
>
> - [没入型翻訳](https://immersivetranslate.com/docs/services/custom/)：`設定`ページの開発者モードで`Beta`機能を有効にすると、`翻訳サービス`の中に`カスタムAPI設定`が表示されます（[公式画像付きチュートリアル](https://immersivetranslate.com/docs/services/custom/)）。その後、`カスタムAPI設定`の`秒間最大リクエスト数`を高く設定して、サーバーの性能を最大限に引き出してください。私は`秒間最大リクエスト数`を`512`、`リクエストごとの最大段落数`を`1`に設定しています。サーバーの構成に合わせて調整してください。
>
> - [Kiss Translator](https://github.com/fishjar/kiss-translator)：`設定`ページでインターフェース設定を下にスクロールすると、カスタムインターフェース `Custom` が表示されます。同様に、`最大同時リクエスト数`と`リクエスト間隔時間`を設定してサーバーの性能を引き出してください。私は`最大同時リクエスト数`を`100`、`リクエスト間隔時間`を`1`に設定しています。サーバーの構成に合わせて調整してください。
>
> 次に、以下の表に従ってプラグインのカスタムインターフェースアドレスを設定します。

| 名前 | URL | プラグイン設定 |
| ---- | --- | -------------- |
| 没入型翻訳（パスワードなし） | `http://localhost:8989/imme` | `カスタムAPI設定` - `API URL` |
| 没入型翻訳（パスワードあり） | `http://localhost:8989/imme?token=your_token` | 同上、URL末尾の `your_token` をあなたの `MT_API_TOKEN` の値に変更してください |
| Kiss Translator（パスワードなし） | `http://localhost:8989/kiss` | `インターフェース設定` - `Custom` - `URL` |
| Kiss Translator（パスワードあり） | `http://localhost:8989/kiss` | 同上、`KEY` に `your_token` を入力してください |
| DeepL 互換 | `http://localhost:8989/deepl` | `DeepL-Auth-Key` または `Bearer` 認証を使用 |
| Google 互換 | `http://localhost:8989/google/language/translate/v2` | `key` パラメータまたは `Bearer` 認証を使用 |
| 划词翻译 | `http://localhost:8989/hcfy` | `token` パラメータまたは `Bearer` 認証をサポート |

**一般ユーザーは、表の内容に従ってプラグインの使用インターフェースアドレスを設定すれば使用できます。**

## 類似プロジェクト

同様の機能を持つプロジェクトをいくつか挙げます。他のニーズがある場合は、これらのプロジェクトを試してみてください。

| プロジェクト名 | メモリ使用量 | 同時実行性能 | 翻訳品質 | 速度 | その他情報 |
| -------------- | ------------ | ------------ | -------- | ---- | ---------- |
| [NLLB](https://github.com/facebookresearch/fairseq/tree/nllb) | 非常に高い | 悪い | 普通 | 遅い | Android移植版の [RTranslator](https://github.com/niedev/RTranslator) は多くの最適化がありますが、それでもリソース使用量が高く、高速ではありません |
| [LibreTranslate](https://github.com/LibreTranslate/LibreTranslate) | 非常に高い | 普通 | 普通 | 中程度 | ミドルレンジCPUで毎秒3文、ハイエンドCPUで毎秒15-20文処理。[詳細](https://community.libretranslate.com/t/performance-benchmark-data/486) |
| [OPUS-MT](https://github.com/OpenNMT/CTranslate2#benchmarks) | 高い | 普通 | やや悪い | 速い | [性能テスト](https://github.com/OpenNMT/CTranslate2#benchmarks) |
| その他大規模モデル | 超高い | 動的 | 非常に良い | 非常に遅い | ハードウェア要件が高い。高同時実行翻訳が必要な場合は、vllmフレームワークの使用をお勧めします。メモリとVRAM使用量で翻訳同時実行数を制御できます。 |
| 本プロジェクト | 低 | 高い | 普通 | 極めて速い | リクエストあたり平均応答時間50ms。 |

> 表のデータはCPU、英中翻訳シナリオでの簡易テストであり、厳密なテストではなく、非量子化バージョンの比較です。参考程度にしてください。

# 高度な設定説明

[API_ja.md](API_ja.md) ファイルおよび起動後の API ドキュメントを参照してください。

## Star History

[![Star History Chart](https://api.star-history.com/svg?repos=xxnuo/MTranServer&type=Timeline)](https://www.star-history.com/#xxnuo/MTranServer&Timeline)

## Thanks

[Bergamot Project](https://browser.mt/) for awesome idea of local translation.

[Mozilla](https://github.com/mozilla) for the [models](https://github.com/mozilla/firefox-translations-models).
