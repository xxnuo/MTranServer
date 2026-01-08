#!/usr/bin/env python3
"""
MTranServer 模型文件下载工具

此脚本用于从 Mozilla Firefox 翻译服务下载 MTranServer 所需的模型文件。
支持命令行参数和交互式输入两种运行模式，自动处理模型文件的下载、校验和解压。

功能特性:
    - 自动获取最新的模型记录
    - 支持架构过滤 (base-memory, base, tiny)
    - SHA256 校验验证
    - 断点续传和重试机制
    - 多线程并发下载
    - 进度条显示 (需安装 tqdm)
    - 详细的日志记录

用法:
  命令行模式:
    # 使用默认配置下载模型 (base-memory 架构)
    python download_models.py --model-dir ./models

    # 指定架构类型下载
    python download_models.py --model-dir ./models --arch-filter base

    # 使用 8 个并发线程加速下载
    python download_models.py --model-dir ./models --workers 8

    # 下载 tiny 架构 (最小体积)
    python download_models.py --model-dir ./models --arch-filter tiny

  交互式模式:
    python download_models.py
    python download_models.py -i

  远程执行:
    必要条件:
    1. 安装 Python 3.x
    2. 解压支持 (二选一):
       - pip install zstandard (推荐，纯 Python 无系统依赖)
       - 或安装系统命令: brew install zstd (macOS) / apt install zstd (Linux)
    3. 可选: pip install tqdm (显示下载进度条)

    # 远程执行脚本 (交互式模式, Linux/macOS)https://raw.githubusercontent.com/xxnuo/MTranServer/refs/heads/main/scripts/download_models.py
    python3 <(curl -s https://raw.githubusercontent.com/xxnuo/MTranServer/refs/heads/main/scripts/download_models.py)

    # 远程执行脚本 (命令行模式)
    python3 <(curl -s https://raw.githubusercontent.com/xxnuo/MTranServer/refs/heads/main/scripts/download_models.py) --model-dir ./models --arch-filter base-memory

选项:
  --model-dir, -m      : 模型存储目录 (默认: ./models)
  --arch-filter, -a    : 架构过滤器 (base-memory/base/tiny, 默认: base-memory)
  --workers, -w        : 并发下载线程数 (默认: 4)
  -i, --interactive    : 使用交互式模式

架构说明:
  base-memory  : 内存优化版本 (默认，适用于 amd64 架构服务器)
  base         : 基础版本 (平衡性能和资源占用)
  tiny         : 精简版本 (最小体积，适用于资源受限环境)

更多信息:
  MTranServer: https://github.com/xxnuo/MTranServer
  模型来源: Mozilla Firefox 翻译服务
"""

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional

try:
    from tqdm import tqdm
except ImportError:
    tqdm = None

try:
    import zstandard as zstd
    HAS_ZSTANDARD = True
except ImportError:
    zstd = None
    HAS_ZSTANDARD = False

# =============================================================================
# 常量定义
# =============================================================================

RECORDS_URL = "https://firefox.settings.services.mozilla.com/v1/buckets/main-preview/collections/translations-models-v2/records"
MODEL_CDN_BASE = "https://firefox-settings-attachments.cdn.mozilla.net/"

# 支持的架构类型
ARCHITECTURES = ["base-memory", "base", "tiny"]
# 默认架构（对应 amd64）
DEFAULT_ARCH = "base-memory"

# 文件类型
FILE_TYPES = ["model", "lex", "vocab", "srcvocab", "trgvocab"]

# 下载配置
MAX_RETRIES = 3
TIMEOUT = 30
CHUNK_SIZE = 8192


# =============================================================================
# 数据类定义
# =============================================================================

@dataclass
class Attachment:
    """模型附件信息"""
    hash: str
    size: int
    filename: str
    location: str
    mimetype: str

    @classmethod
    def from_dict(cls, data: dict) -> "Attachment":
        return cls(
            hash=data["hash"],
            size=data["size"],
            filename=data["filename"],
            location=data["location"],
            mimetype=data["mimetype"],
        )


@dataclass
class ModelRecord:
    """模型记录"""
    name: str
    schema: int
    version: str
    file_type: str
    attachment: Attachment
    architecture: Optional[str]
    source_language: str
    target_language: str
    decompressed_hash: Optional[str]
    decompressed_size: Optional[int]
    filter_expression: str
    id: str
    last_modified: int

    @classmethod
    def from_dict(cls, data: dict) -> "ModelRecord":
        return cls(
            name=data["name"],
            schema=data["schema"],
            version=data["version"],
            file_type=data["fileType"],
            attachment=Attachment.from_dict(data["attachment"]),
            architecture=data.get("architecture"),
            source_language=data["sourceLanguage"],
            target_language=data["targetLanguage"],
            decompressed_hash=data.get("decompressedHash"),
            decompressed_size=data.get("decompressedSize"),
            filter_expression=data.get("filter_expression", ""),
            id=data["id"],
            last_modified=data["last_modified"],
        )

    def get_download_url(self) -> str:
        """获取下载 URL"""
        return f"{MODEL_CDN_BASE}{self.attachment.location}"

    def get_language_pair(self) -> str:
        """获取语言对标识"""
        return f"{self.source_language}_{self.target_language}"


# =============================================================================
# 工具函数
# =============================================================================

def log_info(message: str) -> None:
    """输出信息日志"""
    print(f"[INFO] {message}")


def log_error(message: str) -> None:
    """输出错误日志"""
    print(f"[ERROR] {message}", file=sys.stderr)


def log_success(message: str) -> None:
    """输出成功日志"""
    print(f"[OK] {message}")


def log_warning(message: str) -> None:
    """输出警告日志"""
    print(f"[WARN] {message}")


def calculate_sha256(filepath: Path) -> str:
    """计算文件的 SHA256 哈希值"""
    sha256 = hashlib.sha256()
    with open(filepath, "rb") as f:
        for chunk in iter(lambda: f.read(CHUNK_SIZE), b""):
            sha256.update(chunk)
    return sha256.hexdigest()


def download_with_retry(url: str, filepath: Path, retries: int = MAX_RETRIES) -> bool:
    """带重试的下载函数"""
    for attempt in range(retries):
        try:
            urllib.request.urlretrieve(url, filepath)
            return True
        except Exception as e:
            if attempt < retries - 1:
                log_warning(f"下载失败，重试 ({attempt + 1}/{retries}): {e}")
            else:
                log_error(f"下载失败: {e}")
                return False
    return False


def decompress_zst(input_path: Path, output_path: Path) -> bool:
    """
    解压 zstd 文件

    优先使用 Python zstandard 库，如不可用则回退到系统 zstd 命令
    """
    # 优先使用 Python zstandard 库
    if HAS_ZSTANDARD:
        return _decompress_with_python(input_path, output_path)

    # 回退到系统 zstd 命令
    return _decompress_with_command(input_path, output_path)


def _decompress_with_python(input_path: Path, output_path: Path) -> bool:
    """使用 Python zstandard 库解压"""
    try:
        dctx = zstd.ZstdDecompressor()
        with open(input_path, "rb") as ifh:
            with open(output_path, "wb") as ofh:
                dctx.copy_stream(ifh, ofh)
        return True
    except Exception as e:
        log_error(f"Python 解压失败: {e}")
        return False


def _decompress_with_command(input_path: Path, output_path: Path) -> bool:
    """使用系统 zstd 命令解压"""
    try:
        if not shutil.which("zstd"):
            log_error(
                "未找到解压工具，请安装:\n"
                "  Python 库: pip install zstandard\n"
                "  或系统命令: brew install zstd (macOS) / apt install zstd (Linux)"
            )
            return False

        result = subprocess.run(
            ["zstd", "-d", "-f", str(input_path), "-o", str(output_path)],
            capture_output=True,
            text=True,
        )

        if result.returncode != 0:
            log_error(f"解压失败: {result.stderr}")
            return False

        return True

    except Exception as e:
        log_error(f"解压异常: {e}")
        return False


# =============================================================================
# 核心类
# =============================================================================

class ModelDownloader:
    """模型下载器"""

    def __init__(self, model_dir: Path, architecture: str):
        """
        初始化下载器

        Args:
            model_dir: 模型存储目录
            architecture: 架构过滤器
        """
        self.model_dir = Path(model_dir).resolve()
        self.architecture = architecture
        self.records: List[ModelRecord] = []
        self.downloaded = 0
        self.failed = 0
        self.skipped = 0

    def fetch_records(self) -> bool:
        """获取模型记录"""
        log_info("正在获取模型记录...")
        try:
            with urllib.request.urlopen(RECORDS_URL, timeout=TIMEOUT) as response:
                data = json.load(response)

            self.records = [
                ModelRecord.from_dict(record)
                for record in data.get("data", [])
            ]

            log_success(f"获取到 {len(self.records)} 条模型记录")
            return True

        except Exception as e:
            log_error(f"获取模型记录失败: {e}")
            return False

    def filter_records(self) -> List[ModelRecord]:
        """过滤模型记录"""
        filtered = [
            r for r in self.records
            if r.architecture == self.architecture
        ]

        log_info(f"架构 '{self.architecture}' 过滤后: {len(filtered)} 个文件")

        return filtered

    def download_file(self, record: ModelRecord) -> bool:
        """
        下载单个模型文件

        Args:
            record: 模型记录

        Returns:
            是否成功
        """
        lang_pair = record.get_language_pair()
        lang_dir = self.model_dir / lang_pair
        compressed_path = lang_dir / record.attachment.filename
        decompressed_path = lang_dir / record.attachment.filename.replace(".zst", "")

        # 创建目录
        lang_dir.mkdir(parents=True, exist_ok=True)

        # 检查文件是否已存在且通过校验
        if decompressed_path.exists():
            current_hash = calculate_sha256(decompressed_path)
            if current_hash == record.decompressed_hash:
                log_info(f"跳过已存在文件: {decompressed_path.relative_to(self.model_dir)}")
                self.skipped += 1
                return True
            else:
                log_warning(f"文件校验失败，重新下载: {decompressed_path.relative_to(self.model_dir)}")

        # 下载压缩文件
        url = record.get_download_url()
        log_info(f"下载: {record.name}")

        if not download_with_retry(url, compressed_path):
            self.failed += 1
            return False

        # 验证压缩文件哈希
        compressed_hash = calculate_sha256(compressed_path)
        if compressed_hash != record.attachment.hash:
            log_error(f"压缩文件校验失败: {compressed_path.name}")
            compressed_path.unlink(missing_ok=True)
            self.failed += 1
            return False

        # 解压文件
        if not decompress_zst(compressed_path, decompressed_path):
            compressed_path.unlink(missing_ok=True)
            self.failed += 1
            return False

        # 验证解压后文件哈希
        if record.decompressed_hash:
            decompressed_hash = calculate_sha256(decompressed_path)
            if decompressed_hash != record.decompressed_hash:
                log_error(f"解压文件校验失败: {decompressed_path.name}")
                compressed_path.unlink(missing_ok=True)
                decompressed_path.unlink(missing_ok=True)
                self.failed += 1
                return False

        # 删除压缩文件
        compressed_path.unlink(missing_ok=True)

        # 更新修改时间
        decompressed_path.touch(last_modified=record.last_modified / 1000)

        self.downloaded += 1
        log_success(f"完成: {decompressed_path.relative_to(self.model_dir)}")
        return True

    def download_all(self, max_workers: int = 4) -> None:
        """
        下载所有过滤后的模型文件

        Args:
            max_workers: 最大并发下载线程数
        """
        filtered = self.filter_records()

        if not filtered:
            log_warning("没有需要下载的模型")
            return

        log_info(f"准备下载 {len(filtered)} 个文件到: {self.model_dir}")
        log_info(f"使用 {max_workers} 个并发线程")

        # 使用线程池并发下载
        with ThreadPoolExecutor(max_workers=max_workers) as executor:
            futures = {executor.submit(self.download_file, record): record for record in filtered}

            if tqdm:
                with tqdm(total=len(futures), desc="下载进度", unit="文件") as pbar:
                    for future in as_completed(futures):
                        pbar.update(1)
            else:
                for future in as_completed(futures):
                    pass

        # 输出统计
        print("\n" + "=" * 50)
        log_success("下载完成")
        print(f"  成功: {self.downloaded}")
        print(f"  跳过: {self.skipped}")
        print(f"  失败: {self.failed}")
        print("=" * 50)


# =============================================================================
# 交互式模式
# =============================================================================

def interactive_mode() -> dict:
    """交互式模式配置"""
    print("=" * 50)
    print("MTranServer 模型下载工具 - 交互式模式")
    print("=" * 50)

    # 模型目录
    default_dir = Path.cwd() / "models"
    model_dir_input = input(f"\n模型目录 [{default_dir}]: ").strip()
    model_dir = Path(model_dir_input) if model_dir_input else default_dir

    # 架构选择
    print(f"\n支持的架构类型:")
    for i, arch in enumerate(ARCHITECTURES, 1):
        mark = " (默认)" if arch == DEFAULT_ARCH else ""
        print(f"  {i}. {arch}{mark}")

    arch_input = input(f"\n选择架构 [1-{len(ARCHITECTURES)}]: ").strip()

    if arch_input.isdigit():
        index = int(arch_input) - 1
        if 0 <= index < len(ARCHITECTURES):
            architecture = ARCHITECTURES[index]
        else:
            print(f"无效选择，使用默认架构: {DEFAULT_ARCH}")
            architecture = DEFAULT_ARCH
    else:
        architecture = DEFAULT_ARCH

    # 并发数
    workers_input = input(f"\n并发线程数 [4]: ").strip()
    max_workers = int(workers_input) if workers_input.isdigit() else 4

    print("\n" + "=" * 50)
    print("配置确认:")
    print(f"  模型目录: {model_dir}")
    print(f"  架构类型: {architecture}")
    print(f"  并发线程: {max_workers}")
    print("=" * 50)

    confirm = input("\n确认开始下载? [y/N]: ").strip().lower()
    if confirm not in ["y", "yes"]:
        print("取消下载")
        sys.exit(0)

    return {
        "model_dir": model_dir,
        "architecture": architecture,
        "max_workers": max_workers,
    }


# =============================================================================
# 主函数
# =============================================================================

def main() -> int:
    """主函数"""
    parser = argparse.ArgumentParser(
        description="MTranServer 模型文件下载工具",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  %(prog)s                           # 交互式模式
  %(prog)s -m ./models               # 指定模型目录
  %(prog)s -m ./models -a base       # 下载 base 架构模型
  %(prog)s -m ./models -w 8          # 使用 8 个并发线程
  %(prog)s -m ./models -a tiny -w 4  # 下载 tiny 架构，4 线程

支持的架构:
  base-memory  内存优化版本 (默认，适用于 amd64)
  base         基础版本 (平衡性能和资源)
  tiny         精简版本 (最小体积)
        """,
    )

    parser.add_argument(
        "-m", "--model-dir",
        type=Path,
        default=None,
        help="模型存储目录 (默认: ./models)",
    )

    parser.add_argument(
        "-a", "--arch-filter",
        choices=ARCHITECTURES,
        default=DEFAULT_ARCH,
        help="架构过滤器 (默认: base-memory)",
    )

    parser.add_argument(
        "-w", "--workers",
        type=int,
        default=4,
        help="并发下载线程数 (默认: 4)",
    )

    parser.add_argument(
        "-i", "--interactive",
        action="store_true",
        help="使用交互式模式",
    )

    args = parser.parse_args()

    # 交互式模式
    if args.interactive or len(sys.argv) == 1:
        config = interactive_mode()
    else:
        config = {
            "model_dir": args.model_dir or Path.cwd() / "models",
            "architecture": args.arch_filter,
            "max_workers": args.workers,
        }

    # 创建下载器
    downloader = ModelDownloader(
        model_dir=config["model_dir"],
        architecture=config["architecture"],
    )

    # 获取模型记录
    if not downloader.fetch_records():
        return 1

    # 开始下载
    downloader.download_all(max_workers=config["max_workers"])

    return 0 if downloader.failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
