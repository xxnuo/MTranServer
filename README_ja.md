# MTranServer 
> ミニ翻訳サーバー ベータ版

<img src="./images/logo.jpg" width="auto" height="128" align="right">

[English](README_en.md) | [中文](README.md) | 日本語

CPUと1GBのメモリのみで動作する超軽量・高速なオフライン翻訳サーバーです。GPUは不要で、1リクエストあたりの平均応答時間は50msです。全世界主要言語の翻訳をサポートします。

翻訳品質はGoogle翻訳に匹敵します。

注意：このモデルは速度の最適化と様々なデバイスでのプライベートデプロイメントに重点を置いているため、大規模言語モデルと比べると翻訳品質は一般的な平均水準です。

高品質な翻訳が必要な場合は、オンライン大規模モデルAPIをご利用ください。

<img src="./images/preview.png" width="auto" height="328">

## 類似プロジェクトとの比較（CPU、英語から中国語）

| プロジェクト名 | メモリ使用量 | 同時処理性能 | 翻訳品質 | 速度 | 追加情報 |
|----------------|--------------|--------------|----------|--------|------------|
| [facebook/nllb](https://github.com/facebookresearch/fairseq/tree/nllb) | 非常に高い | 低い | 普通 | 遅い | Android ポート [RTranslator](https://github.com/niedev/RTranslator)は最適化されていますが、リソース使用量は依然として高く、速度も遅いです。 |
| [LibreTranslate](https://github.com/LibreTranslate/LibreTranslate) | 非常に高い | 普通 | 普通 | 中程度 | 中級CPUで3文/秒、高級CPUで15-20文/秒。[詳細](https://community.libretranslate.com/t/performance-benchmark-data/486) |
| [OPUS-MT](https://github.com/OpenNMT/CTranslate2#benchmarks) | 高い | 普通 | やや劣る | 速い | [性能テスト](https://github.com/OpenNMT/CTranslate2#benchmarks) |
| Any LLM | 非常に高い | 動的 | 良い | 非常に遅い | 32B以上のパラメータモデルは効果がありますが、高いハードウェア要件があります |
| MTranServer（本プロジェクト） | 低い | 高い | 普通 | 超高速 | 1リクエストあたりの平均応答時間50ms |

> ※現在のTransformerアーキテクチャの大型モデルの小さなパラメータ量化バージョンは検討の対象外です。実際の調査や使用において、翻訳品質が非常に不安定で、乱翻し、幻覚が深刻で、速度も遅いためです。将来、Diffusionアーキテクチャの言語モデルがリリースされ次第、再度テストを行います。
>
> ※非厳密なテスト、非量子化バージョンの比較、参考値として。

## Docker Composeでのサーバーデプロイ

現在、amd64アーキテクチャCPUのDockerデプロイメントのみをサポートしています。

ARM、RISCVアーキテクチャは開発中です 😳

コンピュータに`Docker Desktop`をインストールし、以下のガイドに従って`Docker Compose`でデプロイすることもできます。

### 1. 準備

設定ファイル用のフォルダを作成し、以下のコマンドを実行します：

```bash
mkdir mtranserver
cd mtranserver
touch config.ini
touch compose.yml
mkdir models
```

### 設定

#### 1.1 `config.ini`をエディタで開き、以下の内容を記述します：
```ini
CORE_API_TOKEN=your_token
```
注意：`your_token`を英数字を使用した独自のパスワードに変更してください。

内部ネットワークでの使用の場合、パスワードの設定は任意ですが、クラウドサーバーの場合は、スキャン、攻撃、乱用から保護するためにパスワードの設定を強く推奨します。

#### 1.2 `compose.yml`をエディタで開き、以下の内容を記述します：

> 注：ポートを変更する場合は、`ports`の値を変更してください。例えば、`8990:8989`に変更すると、サービスポートをローカルポート8990にマッピングします。

```yaml
services:
  mtranserver:
    image: xxnuo/mtranserver:latest
    container_name: mtranserver
    restart: unless-stopped
    ports:
      - "8989:8989"
    volumes:
      - ./models:/app/models
      - ./config.ini:/app/config.ini
```

#### 1.3 オプションの手順

中国本土でイメージを正常にダウンロードできない場合は、以下の手順でイメージをインポートできます：

<a href="https://ocn4e4onws23.feishu.cn/drive/folder/IboFf5DXhl1iPnd2DGAcEZ9qnnd?from=from_copylink" target="_blank">中国本土ダウンロードリンク（Dockerイメージを含む）</a>

`Dockerイメージダウンロード`フォルダに入り、最新のイメージ`mtranserver.image.tar`をDockerマシンにダウンロードします。

ダウンロードディレクトリでターミナルを開き、以下のコマンドを実行してイメージをインポートします：
```bash
docker load -i mtranserver.image.tar
```
その後、通常通り次のステップのモデルのダウンロードに進みます。

### 2. モデルのダウンロード

<a href="https://ocn4e4onws23.feishu.cn/drive/folder/IboFf5DXhl1iPnd2DGAcEZ9qnnd?from=from_copylink" target="_blank">中国本土ダウンロードリンク（Dockerイメージを含む）</a> モデルは`モデルダウンロード`フォルダにあります

<a href="https://github.com/xxnuo/MTranServer/releases/tag/models" target="_blank">国際ダウンロードリンク</a>

各言語の圧縮パッケージを`models`フォルダに展開します。

英中モデルを使用する場合のフォルダ構造例：
```
compose.yml
config.ini
models/
├── enzh
│   ├── lex.50.50.enzh.s2t.bin
│   ├── model.enzh.intgemm.alphas.bin
│   └── vocab.enzh.spm
```

中英と英中モデルを使用する場合のフォルダ構造例：
```
compose.yml
config.ini
models/
├── enzh
│   ├── lex.50.50.enzh.s2t.bin
│   ├── model.enzh.intgemm.alphas.bin
│   └── vocab.enzh.spm
├── zhen
│   ├── lex.50.50.zhen.t2s.bin
│   ├── model.zhen.intgemm.alphas.bin
│   └── vocab.zhen.spm
```

必要なモデルのみをダウンロードしてください。

注意：例えば、中国語から日本語への翻訳は、まず中国語から英語に翻訳し、次に英語から日本語に翻訳するため、`zhen`と`enja`の両方のモデルが必要です。他の言語の翻訳も同様に動作します。

### 3. サービスの起動

まず、モデルが正しく配置され、正常に読み込めること、ポートが使用されていないことを確認するためにテスト実行します。

```bash
docker compose up
```

正常な出力例：
```
[+] Running 2/2
 ✔ Network sample_default  Created  0.1s 
 ✔ Container mtranserver   Created  0.1s 
Attaching to mtranserver
mtranserver  | (2025-03-03 12:49:24) [INFO    ] Using maximum available worker count: 16
mtranserver  | (2025-03-03 12:49:24) [INFO    ] Starting Translation Service
mtranserver  | (2025-03-03 12:49:24) [INFO    ] Service port: 8989
mtranserver  | (2025-03-03 12:49:24) [INFO    ] Worker threads: 16
mtranserver  | Successfully loaded model for language pair: enzh
mtranserver  | (2025-03-03 12:49:24) [INFO    ] Models loaded.
mtranserver  | (2025-03-03 12:49:24) [INFO    ] Using default max parallel translations: 32
mtranserver  | (2025-03-03 12:49:24) [INFO    ] Max parallel translations: 32
```

`Ctrl+C`でサービスを停止し、その後正式に起動します：

```bash
docker compose up -d
```

これでサーバーがバックグラウンドで実行されます。

### 4. API ドキュメント

以下の表の`localhost`は、サーバーアドレスまたはDockerコンテナ名に置き換えることができます。

ポート`8989`は、`compose.yml`で設定したポート値に置き換えることができます。

`CORE_API_TOKEN`が設定されていないか空の場合、翻訳プラグインはパスワードなしのAPIを使用します。

`CORE_API_TOKEN`が設定されている場合、翻訳プラグインはパスワード付きのAPIを使用します。

以下の表の`your_token`は、`config.ini`の`CORE_API_TOKEN`の値に置き換えてください。

#### 翻訳プラグインインターフェース：

> 注：
> 
> - [Immersive Translation](https://immersivetranslate.com/docs/services/custom/) - 設定画面で開発者モードの`Beta`機能を有効にすると、翻訳サービスに`カスタムAPI設定`が表示されます（[公式チュートリアル](https://immersivetranslate.com/docs/services/custom/)）。その後、`カスタムAPI設定`の`1秒あたりの最大リクエスト数`を増やしてサーバーのパフォーマンスを最大限に活用します。私の設定では`1秒あたりの最大リクエスト数`を`5000`に、`1リクエストあたりの最大段落数`を`10`に設定しています。サーバーの構成に応じて調整してください。
> 
> - [Kiss Translator](https://github.com/fishjar/kiss-translator) - 設定ページをスクロールすると、カスタムインターフェース`Custom`が表示されます。同様に、`最大同時リクエスト数`と`リクエスト間隔時間`を設定してサーバーのパフォーマンスを最大限に活用します。私の設定では`最大同時リクエスト数`を`100`に、`リクエスト間隔時間`を`1`に設定しています。サーバーの構成に応じて調整してください。
>
> 以下の表に従ってプラグインのカスタムインターフェースアドレスを設定してください。

| 名前 | URL | プラグイン設定 |
| --- | --- | --- |
| Immersive Translation（パスワードなし） | `http://localhost:8989/imme` | `カスタムAPI設定` - `API URL` |
| Immersive Translation（パスワードあり） | `http://localhost:8989/imme?token=your_token` | 上記と同じ、`your_token`を`CORE_API_TOKEN`の値に変更 |
| Kiss Translator（パスワードなし） | `http://localhost:8989/kiss` | `インターフェース設定` - `Custom` - `URL` |
| Kiss Translator（パスワードあり） | `http://localhost:8989/kiss` | 上記と同じ、`KEY`に`your_token`を入力 |

**一般ユーザーは、上記の表に従ってプラグインインターフェースアドレスを設定するだけで使用を開始できます。以下の「更新方法」に進んでください。**

#### 開発者API：

> ベースURL: `http://localhost:8989`

| 名前 | URL | リクエスト形式 | レスポンス形式 | 認証ヘッダー |
| --- | --- | --- | --- | --- |
| サービスバージョン | `/version` | なし | なし | なし |
| 言語ペア一覧 | `/models` | なし | なし | Authorization: your_token |
| 標準翻訳 | `/translate` | `{"from": "en", "to": "zh", "text": "Hello, world!"}` | `{"result": "你好，世界！"}` | Authorization: your_token |
| バッチ翻訳 | `/translate/batch` | `{"from": "en", "to": "zh", "texts": ["Hello, world!", "Hello, world!"]}` | `{"results": ["你好，世界！", "你好，世界！"]}` | Authorization: your_token |
| ヘルスチェック | `/health` | なし | `{"status": "ok"}` | なし |
| ハートビートチェック | `/__heartbeat__` | なし | `Ready` | なし |
| ロードバランサーハートビート | `/__lbheartbeat__` | なし | `Ready` | なし |

### 更新方法

現在はベータ版のサーバーとモデルのため、問題が発生する可能性があります。定期的な更新を推奨します。

新しいモデルをダウンロードし、元の`models`フォルダに展開して上書きし、サーバーを更新して再起動します：
```bash
docker compose down
docker pull xxnuo/mtranserver:latest
docker compose up -d
```

## その他の情報

Windows、Mac、Linuxのスタンドアロンクライアントソフトウェアバージョン[MTranServerCore](https://github.com/xxnuo/MTranServerCore)は開発中です。しばらくお待ちください。

コンピュータに`Docker Desktop`をインストールし、上記のガイドに従って`Docker Compose`でデプロイすることもできます。

サーバーサイドの翻訳推論フレームワークには、C++で書かれた[marian-nmt](https://github.com/marian-nmt/marian-dev)フレームワークを使用しています。

サーバーAPIのソースコードリポジトリ：[MTranServerCore](https://github.com/xxnuo/MTranServerCore)（まだ完成していません。しばらくお待ちください）

## サポート

[コーヒーを奢る ☕️](https://www.creem.io/payment/prod_3QOnrHlGyrtTaKHsOw9Vs1)

[中国本土 💗 Afdian](https://afdian.com/a/xxnuo)

---

WeChat: x-xnuo

X: [@realxxnuo](https://x.com/realxxnuo)

技術やオープンソースプロジェクトについて気軽にご連絡ください！

現在、求職中です。履歴書をご覧になりたい方はご連絡ください。

--- 