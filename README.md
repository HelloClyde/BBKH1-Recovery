# BBKH1-Recovery

步步高 H1 **V1.41 超级系统恢复程序的 Windows 10 x64 兼容层**。保留官方恢复界面，使用 WinUSB 替代原来的 32 位 WinDriver 驱动，支持在解压目录内自动准备驱动和启动恢复。

当前源码版本：**0.2.2**。适用于已确认的 H1 V1.41 恢复流程；不构成 V2.10/V2.20 降级或其他硬件版本的兼容保证。

## 快速开始

成品由本仓库的 GitHub Releases 分发：

- **版本 Release**：下载 `H1-V1.41-Win10x64-v0.2.2-complete.zip`，完整解压后双击 **`启动恢复.cmd`**。管理员提示由 Windows 正常显示；不需要打开 Zadig 或手动点 Install Driver。
- **`official-v1.41` Release**：提供未修改的 `H1-V141-super-recovery.rar`。这是官方闭源原包，CI 下载并校验它，然后与本仓库编译产物整合。
- `H1-Win10x64-v0.2.2-compatibility.zip` 是兼容层单独包，适合开发和手动替换 DLL；普通用户使用完整 ZIP。

H1 按已验证的方式进入 USB Boot：保持 USB 连接，按住 Shift，轻点 RESET，再松开 Shift。完整包会准备 `601A:4740` 和 `0888:A880` 两组 WinUSB 绑定；普通 USB 磁盘模式使用另一组设备身份。

恢复入口可能在连接成功后直接开始擦写设备。具体操作、备份位置、日志及故障处理见 [使用说明](docs/USER_GUIDE.zh-CN.md)。仅核对文件可使用 `仅校验文件.cmd`，它不访问 H1。

开发者在 Windows x64 上构建：安装 Python 3.11 或更新版本和 Git，使用系统自带 tar（7-Zip 可选），然后运行：

```powershell
git clone --recursive https://github.com/HelloClyde/BBKH1-Recovery.git
cd BBKH1-Recovery
python -m pip install -r requirements.txt
python scripts/build.py all --official .cache/H1-V141-super-recovery.rar
```

`--official` 指向从 `official-v1.41` Release 下载的原包。未传该参数时，脚本从本仓库的该 Release 下载原包。脚本验证所有固定依赖，构建、离线测试并生成 `dist/`；**不会安装驱动、启动官方恢复程序或刷写 H1**。更多参数见 [构建说明](docs/BUILD.zh-CN.md)。

## 截图

兼容层自身没有独立图形界面，沿用官方恢复窗口。本仓库尚未收录可公开的新版本运行截图；本轮设备已恢复正常开机，没有为截图再次进行恢复。实机结果和离线验证分别记录于 [验证说明](docs/VALIDATION.zh-CN.md)，并保留这个截图缺项。

## 主要修正

- **Windows 10 x64**：用 x86 ABI 兼容 DLL 对接系统 WinUSB，不加载原 32 位 USB 内核驱动。
- **读取用户信息卡在 0%**：首次 BURN 端点为 64 字节时，仅进行一次预复位，等待 512 字节端点稳定后再交给官方程序；失败时停止接入。
- **恢复用户资料时日期异常**：额外 Windows 文件属性会把两位字段变成四位，使 DOS 日期错位。0.2.2 核对两处原始指令，在电脑程序内存中将属性限定为低 8 位，保留正确的两位格式。
- **一键启动**：校验文件、备份被替换文件、在当前解压目录部署、准备两组驱动并启动原程序；重复启动会被阻止。

## 依赖

| 用途 | 固定输入 |
| --- | --- |
| 使用完整包 | Windows x64、对应 H1、USB 连接和供电；无需 Python/编译器 |
| 官方恢复内容 | H1 V1.41 原始 RAR，467,351,318 字节；SHA-256 见锁定清单 |
| C 工具链 | w64devkit **2.11.0**，下载文件 SHA-256 固定 |
| 驱动准备 | libwdi **1.5.1**，子模块提交 `9b23b82a2dd1cbffc16d46c212f92c6bf8c0c602` |
| 驱动资源 | 官方 Zadig **2.9**，只解包，不执行；资源逐一校验 |
| 解包 | UPX **5.0.2**、innoextract **1.9**、Windows tar；7z SFX 无本机 7-Zip 时使用锁定的 **7zr 26.06** |
| 构建/离线测试 | Python 3.11+、固定版本 pefile 和 Unicorn |

这是 Windows 电脑端工具，**不依赖 BDA SDK**。Git 只跟踪源码、文档和依赖锁定清单；固件、工具链、生成二进制和用户资料不入库。

## CI 与 Release

main/PR/手动构建运行相同的构建和离线检查。`v*` 标签在版本匹配、构建测试和 ZIP 校验通过后，用**同一份 CI 产物**创建版本 Release，包含完整包、兼容层包、完整源码包、验证记录和 SHA256SUMS。

官方原包另放 `official-v1.41` Release；CI 根据固定大小和哈希校验，不依赖原下载站在线。发布 job 单独取得写权限，普通构建保持只读。CI 没有连接 H1，不将编译或模拟检查描述为刷机成功。

本机已完成“0.2.1 系统写入＋日期异常后的两份用户文件续接”，用户确认正常进入桌面。0.2.2 合入日期修正后，完整从头刷机尚未重复验收。详细边界见验证说明。

## 感谢

- HelloClyde：H1 兼容层、故障复现、构建测试和开源整理。
- Pete Batard 及 libwdi/Zadig 贡献者：WinUSB 驱动准备库及安装工具。
- w64devkit、MinGW-w64、UPX、Unicorn 和 pefile 项目及贡献者。
- 提供 H1 实机测试、报错截图及日志的使用者。
- 步步高原始 H1 恢复工具的开发者。

自有代码采用 **GPL-3.0-or-later**。官方恢复程序和第三方组件保持原许可，详见 [NOTICE](NOTICE.md)。
