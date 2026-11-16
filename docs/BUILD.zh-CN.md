# 构建与发布

支持 Windows x64。首次构建安装 Git、Python 3.11+，并准备 Windows 自带 tar。可选安装 7-Zip；脚本会在需要时取得固定的便携 7zr。初始化固定子模块：

```powershell
git submodule update --init --recursive
python -m pip install -r requirements.txt
python scripts/build.py all
```

`all` 相当于依次执行 `bootstrap`、`build`、`bundle`、`test`、`package`。未提供本地原包时，会从本仓库 `official-v1.41` Release 下载固定的官方 RAR。也可显式指定：

```powershell
python scripts/build.py all --official C:/Downloads/H1-V141-super-recovery.rar
```

自定义工具路径：`--cc32`、`--cc64`、`--sevenzip`。默认工具链按锁定清单下载到 `.cache/toolchain/w64devkit`；所有路径以仓库为基准，不依赖当前工作目录、开发者用户名或 SDK 绝对位置。

官方 RAR 中包含一个 Inno 安装程序：脚本先使用系统 tar 解出它，再用锁定的 innoextract 1.9 提取文件，不执行安装程序。构建阶段也只解包 Zadig，按固定偏移、长度和 SHA-256 提取未修改的驱动资源。libwdi DLL 与 CLI 从固定子模块源码编译，UTF-8 入口适配也从本仓库源码编译。构建过程不运行 Zadig，不装驱动，不启动官方恢复程序。

输出：

- `build/bridge/`：兼容、只读及假后端 DLL、检查与测试程序。
- `build/driver/`：从源码编译的 libwdi 与 CLI。
- `build/bundle/`：可用的完整包目录；生成清单绑定到 H1Setup.exe。
- `build/verification.json`：本次离线测试结果。
- `dist/`：完整 ZIP、兼容层 ZIP、包含 libwdi 的源码 ZIP、验证 JSON 及校验文件。

包内压缩条目采用固定时间，PE 链接关闭插入时间戳；编译器、依赖和源码有锁定指纹。不同环境产物以该次构建记录和 SHA256SUMS 为准。

## 发布流程

仓库管理员先将锁定的原始 RAR 与校验文件放到 `official-v1.41` Release。官方包不提交到 Git，也不套用源码许可证。

修改 `VERSION` 和发布说明，提交后推送匹配的 `vMAJOR.MINOR.PATCH` 标签。Actions 会核对标签版本、下载原包、从源码构建、测试、整合与校验，然后由单独的发布 job 上传本次构建产物。普通分支、PR 与在分支上手动运行只产生 Actions artifacts；在版本标签上手动运行也可以完成发布。

首次建仓需要先上传官方原包，因此初始提交可跳过自动 CI；原包归档完成后，在对应版本标签上手动触发同一工作流。后续版本正常推送标签即可。

已有标签和同名资产不会被自动覆盖。0.2.2 的首次开源版本标为预发布，保留日期修正未再次整轮刷机的验证边界。
