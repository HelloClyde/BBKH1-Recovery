# 来源与许可

项目源码及同版本完整源码归档由 `https://github.com/HelloClyde/BBKH1-Recovery` 提供。源码 Release 资产包含固定的 libwdi 源码和本项目构建脚本。

- 本仓库的 H1 WinUSB 兼容层、备份属性修正、一键准备程序、构建和测试代码，由 HelloClyde 项目整理发布，采用 GPL-3.0-or-later，见 `LICENSE`。
- `third_party/libwdi` 固定到 `9b23b82a2dd1cbffc16d46c212f92c6bf8c0c602`（v1.5.1）。作者包括 Pete Batard 等上游贡献者，库和 `wdi-simple` 示例遵循各自文件中的 LGPL-3.0-or-later 声明。保留其 COPYING、COPYING-LGPL 和源码版权信息。
- `src/driver/wdi_utf8.c` 提供 Windows UTF-16 参数到 UTF-8 的入口适配；与上游未经修改的 `wdi-simple.c` 一同构建，原始上游源码通过固定子模块和成品包内源码归档提供。
- Microsoft WinUSB/WDF 共同安装组件为未修改的第三方二进制，不采用本项目 GPL 许可。资源取自官方 Zadig 2.9，来源、大小和 SHA-256 固定在 `dependencies.lock.json`。Zadig 仅被解包以取得资源，构建过程不会执行 Zadig 或安装驱动。libwdi 的安装辅助程序源码在固定子模块内。
- 官方 H1 V1.41 恢复程序、帮助、配置和固件数据属于其原作者，不是本项目开源源码。`official-v1.41` Release 原样归档官方 RAR；整合包保留原始 `license.txt`、帮助及旧驱动文件。兼容层通过替换电脑端 DLL 及核对后的内存指令修正工作，不将官方程序或固件重新许可为 GPL。
- 原始恢复包来源：`http://down.eebbk.net/xzzx/y100/1/@ibox H1 V1.41CJXTHF.rar`。锁定 SHA-256：`b1f5f4d886c1c08c7d6f0722581615a7262cfe44b62f1f1e47eef204f5e5e5db`。
- w64devkit 2.11.0、UPX 5.0.2、7zr 26.06、innoextract 1.9、Python 和构建库是独立构建依赖，各自保留上游许可。工具链不提交到 Git。

本项目与步步高、Microsoft、Jungo 和其他上游项目无官方隶属关系；相关名称仅用于识别设备、接口和来源。仓库及 Release 不包含设备用户备份、私人存档、RAM/NAND 转储或本机日志。
