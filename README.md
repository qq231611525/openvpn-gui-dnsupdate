# OpenVPN GUI 修改版

基于官方 [OpenVPN GUI](https://github.com/OpenVPN/openvpn-gui) 11.66 源码的修改版
（GPLv2，见 `COPYING`）。新增 DNS 自动更新服务器地址功能。

## 改动说明

1. **连接时自动从 DNS 更新服务器地址**（`dnsupdate.c` / `dnsupdate.h`）
   - 点"连接"时，若该配置记住了 DNS 域名，自动读取其 TXT 记录，
     解析出 `IP:端口`（支持 `1.2.3.4:1194` 和 `1.2.3.4 1194` 两种写法），
     改写配置文件中的 `remote` 行后再连接。全程静默，失败则按原配置继续连接。
   - 托盘菜单新增"更新服务器地址端口…"，用于设置/修改记住的域名
     （按每个配置单独记住，存在注册表）。
2. **URL 导入不再强制填用户名**（`as.c`）：普通 URL 导入只填链接即可，
   用户名仅在 Access Server 导入时必填。
3. **默认静默连接**（`registry.c`）：`silent_connection` 默认值改为 1，
   连接时不弹状态窗口（可在"选项"里改回去）。

## 构建（Linux 交叉编译 Windows x64）

```bash
sudo apt-get install -y mingw-w64
./build-win64.sh
# 产物：build-win64/openvpn-gui.exe
```

纯逻辑单元测试（TXT 解析、`remote` 改写），Linux 下直接跑：

```bash
gcc -Wall -Wextra -DDNSUPDATE_STANDALONE_TEST -o dnsupdate_test dnsupdate.c && ./dnsupdate_test
```

## 安装

1. 右键托盘图标 → 退出，任务管理器确认 `openvpn-gui.exe` 进程已结束
2. 备份 `C:\Program Files\OpenVPN\bin\openvpn-gui.exe`
3. 把新编译的 `openvpn-gui.exe` 复制进去覆盖，重启

注：自编译无数字签名，Windows SmartScreen 警告选"仍要运行"即可。

## 许可证

GPLv2（`COPYING`）。分发修改版二进制时请一并提供本源码。
