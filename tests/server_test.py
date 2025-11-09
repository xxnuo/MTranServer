"""
MTranServer 测试脚本
测试服务器的各个接口功能
"""

import httpx
import time
from typing import Any


class MTranServerTester:
    """MTranServer 测试类"""

    def __init__(self, base_url: str = "http://localhost:8989", api_token: str = ""):
        self.base_url = base_url
        self.api_token = api_token
        self.client = httpx.Client(timeout=30.0)
        self.headers = {}
        if api_token:
            self.headers["Authorization"] = f"Bearer {api_token}"

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.client.close()

    def print_result(self, test_name: str, success: bool, message: str = "", data: Any = None):
        """打印测试结果"""
        status = "✓" if success else "✗"
        print(f"\n[{status}] {test_name}")
        if message:
            print(f"    {message}")
        if data:
            print(f"    数据: {data}")

    def test_health(self) -> bool:
        """测试健康检查接口"""
        try:
            response = self.client.get(f"{self.base_url}/health")
            success = response.status_code == 200
            self.print_result(
                "健康检查",
                success,
                f"状态码: {response.status_code}",
                response.json() if success else None
            )
            return success
        except Exception as e:
            self.print_result("健康检查", False, f"错误: {str(e)}")
            return False

    def test_version(self) -> bool:
        """测试版本接口"""
        try:
            response = self.client.get(f"{self.base_url}/version")
            success = response.status_code == 200
            self.print_result(
                "版本信息",
                success,
                f"状态码: {response.status_code}",
                response.json() if success else None
            )
            return success
        except Exception as e:
            self.print_result("版本信息", False, f"错误: {str(e)}")
            return False

    def test_languages(self) -> bool:
        """测试语言列表接口"""
        try:
            response = self.client.get(f"{self.base_url}/languages", headers=self.headers)
            success = response.status_code == 200
            data = response.json() if success else None
            self.print_result(
                "语言列表",
                success,
                f"状态码: {response.status_code}, 支持语言数: {len(data.get('languages', [])) if data else 0}",
                data
            )
            return success
        except Exception as e:
            self.print_result("语言列表", False, f"错误: {str(e)}")
            return False

    def test_translate(self, text: str = "Hello, world!", from_lang: str = "en", to_lang: str = "zh-Hans") -> bool:
        """测试单文本翻译接口"""
        try:
            start_time = time.time()
            response = self.client.post(
                f"{self.base_url}/translate",
                json={"from": from_lang, "to": to_lang, "text": text},
                headers=self.headers
            )
            elapsed = (time.time() - start_time) * 1000
            success = response.status_code == 200
            data = response.json() if success else None
            self.print_result(
                f"单文本翻译 ({from_lang} -> {to_lang})",
                success,
                f"状态码: {response.status_code}, 耗时: {elapsed:.2f}ms",
                {"原文": text, "译文": data.get("result") if data else None}
            )
            return success
        except Exception as e:
            self.print_result(f"单文本翻译 ({from_lang} -> {to_lang})", False, f"错误: {str(e)}")
            return False

    def test_translate_batch(self, texts: list[str] = None, from_lang: str = "en", to_lang: str = "zh-Hans") -> bool:
        """测试批量翻译接口"""
        if texts is None:
            texts = ["Hello, world!", "Good morning!", "How are you?"]
        
        try:
            start_time = time.time()
            response = self.client.post(
                f"{self.base_url}/translate/batch",
                json={"from": from_lang, "to": to_lang, "texts": texts},
                headers=self.headers
            )
            elapsed = (time.time() - start_time) * 1000
            success = response.status_code == 200
            data = response.json() if success else None
            self.print_result(
                f"批量翻译 ({from_lang} -> {to_lang})",
                success,
                f"状态码: {response.status_code}, 文本数: {len(texts)}, 总耗时: {elapsed:.2f}ms, 平均: {elapsed/len(texts):.2f}ms",
                {"原文": texts, "译文": data.get("results") if data else None}
            )
            return success
        except Exception as e:
            self.print_result(f"批量翻译 ({from_lang} -> {to_lang})", False, f"错误: {str(e)}")
            return False

    def test_google_compat(self, text: str = "The Great Pyramid of Giza", from_lang: str = "en", to_lang: str = "zh-Hans") -> bool:
        """测试 Google 翻译兼容接口"""
        try:
            start_time = time.time()
            response = self.client.post(
                f"{self.base_url}/language/translate/v2",
                json={"q": text, "source": from_lang, "target": to_lang, "format": "text"},
                headers=self.headers
            )
            elapsed = (time.time() - start_time) * 1000
            success = response.status_code == 200
            data = response.json() if success else None
            result = data.get("data", {}).get("translations", [{}])[0].get("translatedText") if data else None
            self.print_result(
                f"Google 兼容接口 ({from_lang} -> {to_lang})",
                success,
                f"状态码: {response.status_code}, 耗时: {elapsed:.2f}ms",
                {"原文": text, "译文": result}
            )
            return success
        except Exception as e:
            self.print_result(f"Google 兼容接口 ({from_lang} -> {to_lang})", False, f"错误: {str(e)}")
            return False

    def test_imme_plugin(self, texts: list[str] = None, from_lang: str = "en", to_lang: str = "zh-Hans") -> bool:
        """测试沉浸式翻译插件接口"""
        if texts is None:
            texts = ["Hello, world!", "Good morning!"]
        
        try:
            url = f"{self.base_url}/imme"
            if self.api_token:
                url += f"?token={self.api_token}"
            
            start_time = time.time()
            response = self.client.post(
                url,
                json={"from": from_lang, "to": to_lang, "trans": texts}
            )
            elapsed = (time.time() - start_time) * 1000
            success = response.status_code == 200
            data = response.json() if success else None
            self.print_result(
                f"沉浸式翻译插件 ({from_lang} -> {to_lang})",
                success,
                f"状态码: {response.status_code}, 文本数: {len(texts)}, 耗时: {elapsed:.2f}ms",
                {"原文": texts, "译文": data.get("trans") if data else None}
            )
            return success
        except Exception as e:
            self.print_result(f"沉浸式翻译插件 ({from_lang} -> {to_lang})", False, f"错误: {str(e)}")
            return False

    def test_kiss_plugin(self, text: str = "Hello, world!", from_lang: str = "en", to_lang: str = "zh-Hans") -> bool:
        """测试简约翻译插件接口"""
        try:
            headers = {}
            if self.api_token:
                headers["KEY"] = self.api_token
            
            start_time = time.time()
            response = self.client.post(
                f"{self.base_url}/kiss",
                json={"from": from_lang, "to": to_lang, "text": text},
                headers=headers
            )
            elapsed = (time.time() - start_time) * 1000
            success = response.status_code == 200
            data = response.json() if success else None
            self.print_result(
                f"简约翻译插件 ({from_lang} -> {to_lang})",
                success,
                f"状态码: {response.status_code}, 耗时: {elapsed:.2f}ms",
                {"原文": text, "译文": data.get("text") if data else None}
            )
            return success
        except Exception as e:
            self.print_result(f"简约翻译插件 ({from_lang} -> {to_lang})", False, f"错误: {str(e)}")
            return False

    def test_performance(self, count: int = 10) -> bool:
        """测试性能 - 连续翻译多次"""
        try:
            text = "Hello, world!"
            times = []
            
            print(f"\n[性能测试] 连续翻译 {count} 次...")
            for i in range(count):
                start_time = time.time()
                response = self.client.post(
                    f"{self.base_url}/translate",
                    json={"from": "en", "to": "zh-Hans", "text": text},
                    headers=self.headers
                )
                elapsed = (time.time() - start_time) * 1000
                times.append(elapsed)
                if response.status_code == 200:
                    print(f"    第 {i+1} 次: {elapsed:.2f}ms")
                else:
                    print(f"    第 {i+1} 次: 失败 (状态码: {response.status_code})")
                    return False
            
            avg_time = sum(times) / len(times)
            min_time = min(times)
            max_time = max(times)
            
            self.print_result(
                "性能测试",
                True,
                f"平均: {avg_time:.2f}ms, 最快: {min_time:.2f}ms, 最慢: {max_time:.2f}ms"
            )
            return True
        except Exception as e:
            self.print_result("性能测试", False, f"错误: {str(e)}")
            return False

    def run_all_tests(self):
        """运行所有测试"""
        print("=" * 60)
        print("MTranServer 测试开始")
        print("=" * 60)
        print(f"服务器地址: {self.base_url}")
        print(f"API Token: {'已设置' if self.api_token else '未设置'}")
        
        results = []
        
        # 基础接口测试
        print("\n" + "=" * 60)
        print("基础接口测试")
        print("=" * 60)
        results.append(("健康检查", self.test_health()))
        results.append(("版本信息", self.test_version()))
        results.append(("语言列表", self.test_languages()))
        
        # 翻译接口测试
        print("\n" + "=" * 60)
        print("翻译接口测试")
        print("=" * 60)
        results.append(("单文本翻译 (英->中)", self.test_translate("Hello, world!", "en", "zh-Hans")))
        results.append(("单文本翻译 (中->英)", self.test_translate("你好，世界！", "zh-Hans", "en")))
        results.append(("批量翻译", self.test_translate_batch()))
        results.append(("Google 兼容接口", self.test_google_compat()))
        
        # 插件接口测试
        print("\n" + "=" * 60)
        print("插件接口测试")
        print("=" * 60)
        results.append(("沉浸式翻译插件", self.test_imme_plugin()))
        results.append(("简约翻译插件", self.test_kiss_plugin()))
        
        # 性能测试
        print("\n" + "=" * 60)
        print("性能测试")
        print("=" * 60)
        results.append(("性能测试", self.test_performance(10)))
        
        # 汇总结果
        print("\n" + "=" * 60)
        print("测试结果汇总")
        print("=" * 60)
        passed = sum(1 for _, success in results if success)
        total = len(results)
        print(f"\n通过: {passed}/{total}")
        
        for name, success in results:
            status = "✓" if success else "✗"
            print(f"  [{status}] {name}")
        
        print("\n" + "=" * 60)
        if passed == total:
            print("所有测试通过！")
        else:
            print(f"有 {total - passed} 个测试失败")
        print("=" * 60)
        
        return passed == total


def main():
    """主函数"""
    import sys
    
    # 从命令行参数获取配置
    base_url = sys.argv[1] if len(sys.argv) > 1 else "http://localhost:8989"
    api_token = sys.argv[2] if len(sys.argv) > 2 else ""
    
    with MTranServerTester(base_url, api_token) as tester:
        success = tester.run_all_tests()
        sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
