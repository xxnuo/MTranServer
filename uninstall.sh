#!/bin/bash
# 卸载 mtranserver 服务

echo "正在卸载 mtranserver.service 服务..."

# 停止服务
echo "停止服务..."
systemctl --user stop mtranserver.service

# 禁用服务（取消开机自启）
echo "禁用开机自启..."
systemctl --user disable mtranserver.service

# 删除服务文件
echo "删除服务文件..."
rm -f ~/.config/systemd/user/mtranserver.service

# 重载配置
echo "重载 systemd 配置..."
systemctl --user daemon-reload

# 重置失败状态（如果有）
systemctl --user reset-failed 2>/dev/null

echo "卸载完成！"
echo ""
echo "提示："
echo "- 如需删除应用程序本身，请手动删除相关文件"
echo "- 如需查看历史日志：journalctl --user -u mtranserver.service"
