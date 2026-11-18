## v5.4.6

- 使用 GitHub Actions 内置令牌创建 GitHub Release，修复发布时的凭据错误。
- 推送版本标签会自动发布 npm 包和 Docker 镜像。npm 使用 OIDC Trusted Publishing。
- Intel Mac 的 CI 在缓存未命中时，下载并校验项目中保存的 ONNX Runtime 1.30.0 构建。

### English

- Use the built-in GitHub Actions token to create GitHub releases and fix the credential error.
- Pushing a version tag automatically publishes the npm package and Docker images. npm uses OIDC trusted publishing.
- On an Intel Mac CI cache miss, download and verify the archived ONNX Runtime 1.30.0 build.

更早的更新见 [HISTORY.md](HISTORY.md)。
