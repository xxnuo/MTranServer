# CPU 翻译模型调研与接入建议

更新时间：2026-08-05

## 结论

可以在不使用 GPU 的情况下运行更大的翻译模型，但它们不能直接放进当前 MTranServer 的模型目录。

当前项目使用 Bergamot WASM，优势是启动快、内存占用低、跨平台简单；代价是模型规模和翻译质量有限。更大的 Hugging Face 模型通常需要新的推理运行时，推荐通过独立的 Python 服务接入 CTranslate2。

第一阶段建议接入 `facebook/m2m100_1.2B` 的 CPU INT8 版本，保留当前 Bergamot 作为低配置回退。机器性能更高时，再增加 `google/madlad400-3b-mt` 的 Q4K 版本。

## 候选模型

| 模型 | 适合场景 | CPU/内存估算 | 许可证 | 接入方式 |
| --- | --- | --- | --- | --- |
| [M2M100 1.2B](https://huggingface.co/facebook/m2m100_1.2B) | 多语言专业翻译，约 100 种语言 | 原始权重约 4.96 GB；INT8 后约 1.5-2.5 GB；建议整机内存 8 GB 以上 | MIT | CTranslate2 CPU INT8 |
| [MADLAD-400-3B-MT](https://huggingface.co/google/madlad400-3b-mt) | 400+ 语言，追求更高质量 | 官方 `model-q4k.gguf` 约 1.65 GB；建议整机内存 8-16 GB | Apache-2.0 | Rust/T5 量化运行时，接入成本较高 |
| [M2M100 418M](https://huggingface.co/facebook/m2m100_418M) | 中低配置的多语言翻译 | INT8 后约 0.7-1.2 GB；建议整机内存 4 GB 以上 | MIT | CTranslate2 CPU INT8 |
| [OPUS-MT en-zh](https://huggingface.co/Helsinki-NLP/opus-mt-en-zh) | 英译中 | 原始模型约 312 MB | Apache-2.0 | CTranslate2 Marian |
| [OPUS-MT zh-en](https://huggingface.co/Helsinki-NLP/opus-mt-zh-en) | 中译英 | 原始模型约 312 MB | CC-BY-4.0 | CTranslate2 Marian |
| [NLLB-200 distilled 600M](https://huggingface.co/facebook/nllb-200-distilled-600M) | 200 种语言的研究和非商业部署 | INT8 CPU 可运行；建议整机内存 6-8 GB | CC-BY-NC-4.0 | CTranslate2 CPU INT8 |
| [Qwen3-8B](https://huggingface.co/Qwen/Qwen3-8B) | 术语、上下文和格式重写 | Q4 量化通常需要约 5-8 GB；建议整机内存 16 GB 以上 | Apache-2.0 | llama.cpp/GGUF 或其他 LLM runtime |

### 首选：M2M100 1.2B

M2M100 是专门的多语言翻译模型，一个模型可以处理中英双向翻译，不需要分别部署两个方向的权重。CTranslate2 官方文档提供了 M2M100 转换示例，并支持 CPU INT8 推理。

推荐转换方式：

```bash
pip install ctranslate2 transformers sentencepiece
ct2-transformers-converter \
  --model facebook/m2m100_1.2B \
  --quantization int8 \
  --output_dir models/m2m100-1.2b-int8
```

### 高质量候选：MADLAD-400-3B-MT

MADLAD 是 T5 架构的专业翻译模型，覆盖 400 多种语言。官方模型卡提供了 CPU 用法，并提供量化后的 `model-q4k.gguf`，文件大小约 1.65 GB，原始权重约 11.8 GB。

它不属于当前 Bergamot 格式，也不是直接给 `llama.cpp` 使用的普通 LLM GGUF。接入需要验证官方 Rust/T5 runtime，工程成本明显高于 M2M100，因此适合作为第二阶段模型。

### 中英专用：OPUS-MT

如果产品早期只做中英文互译，OPUS-MT 的速度、体积和部署成本最好。它需要分别安装 `en-zh` 和 `zh-en` 两个模型，翻译质量和上下文能力通常不如 M2M100 1.2B，但适合 4 GB 左右内存的机器。

## 不推荐作为默认模型的候选

### NLLB-200

NLLB 的技术路线适合 CPU，CTranslate2 也提供了转换示例，但 `CC-BY-NC-4.0` 明确限制商业用途。项目代码使用 Apache-2.0 并不能解除模型本身的非商业限制，因此不应作为可商业发布版本的默认模型。

### Qwen3-8B

Qwen3-8B 是通用大语言模型，不是专门的机器翻译模型。它在长上下文、术语约束、格式修复方面有优势，但 CPU 推理速度慢，输出受提示词和解码参数影响更大。适合做可选的“翻译后编辑”或术语增强，不建议替换稳定的翻译后端。

### TranslateGemma

`google/translategemma-4b-it`、`12b-it` 和 `27b-it` 是专门的翻译模型，但使用 Gemma 专用条款并需要 Hugging Face 授权访问，不适合作为当前开源项目的默认依赖。

## 与当前 MTranServer 的兼容性

当前 Bergamot loader 需要以下文件：

```text
model
lex
srcvocab
trgvocab
```

以下文件不能直接被现有 loader 加载：

```text
*.bin
*.safetensors
*.gguf
通用 ONNX 模型
```

建议增加统一的后端接口：

```text
TranslationBackend
  ├─ BergamotBackend       当前 WASM 模型
  ├─ CTranslate2Backend   M2M100、OPUS-MT、NLLB
  └─ MadladBackend         MADLAD 专用 runtime
```

Web 部署不需要 Electron，推荐的进程关系如下：

```text
React Web
   |
Node/Express API
   |
翻译后端路由
   ├─ Bergamot WASM
   └─ Python + CTranslate2 worker
```

模型目录建议保留运行时和量化信息：

```text
models/
  en_zh-Hans/
    bergamot-base-memory/
  m2m100-1.2b-int8/
  madlad-3b-q4k/
```

模型管理记录至少包含：

```text
model_id
runtime
architecture
quantization
languages
license
license_url
download_url
ram_recommendation
installed_size
```

## 按机器配置推荐

| 整机内存 | 默认推荐 | 说明 |
| --- | --- | --- |
| 4 GB | 当前 Bergamot 或 OPUS-MT | 启动快，适合低资源设备 |
| 8 GB | M2M100 418M INT8 或 M2M100 1.2B INT8 | 质量和资源占用较平衡 |
| 16 GB 以上 | MADLAD-400-3B Q4K 或 M2M100 1.2B INT8 | MADLAD 更大、更慢，适合高质量翻译 |
| 16 GB 以上且需要后编辑 | M2M100/MADLAD + Qwen3-8B Q4 | Qwen 只处理术语和格式修订，不承担默认翻译 |

CPU-only 并不等于不占内存。模型权重、词表、运行时工作区、输入上下文和并发请求都会占用内存；模型越大，吞吐量通常越低。建议每个模型进程只加载一份权重，并通过队列限制并发。

## 许可证与开源发布

当前项目许可证是 Apache-2.0。模型许可证独立于项目代码，不能因为项目是 Apache-2.0 就自动改变模型权重的许可证。

- Apache-2.0、MIT：允许修改、商业使用和再发布，需要保留版权与许可证文本。
- CC-BY-4.0：允许商业使用，但必须署名并注明修改。
- CC-BY-NC-4.0：禁止商业使用，不适合商业发行版默认捆绑。
- Gemma 条款：需要单独审核使用、分发和服务化条件。

发布模型时建议：

1. 在模型管理页面显示模型名称、来源、许可证和原始链接。
2. 在仓库中增加 `MODEL_LICENSES.md`，保存每个模型的版权声明和许可证文本。
3. 对不同许可证的模型分开下载和安装，不把受限模型打包进默认发行包。
4. 保留运行时依赖的许可证信息，例如 CTranslate2、SentencePiece 和对应 tokenizer。

## 官方资料

- [CTranslate2 Transformers 指南](https://opennmt.net/CTranslate2/guides/transformers.html)
- [MADLAD-400-3B-MT 模型卡](https://huggingface.co/google/madlad400-3b-mt)
- [M2M100 1.2B 模型卡](https://huggingface.co/facebook/m2m100_1.2B)
- [M2M100 418M 模型卡](https://huggingface.co/facebook/m2m100_418M)
- [NLLB-200 600M 模型卡](https://huggingface.co/facebook/nllb-200-distilled-600M)
- [OPUS-MT 英译中模型卡](https://huggingface.co/Helsinki-NLP/opus-mt-en-zh)
- [OPUS-MT 中译英模型卡](https://huggingface.co/Helsinki-NLP/opus-mt-zh-en)
- [Qwen3-8B 模型卡](https://huggingface.co/Qwen/Qwen3-8B)
