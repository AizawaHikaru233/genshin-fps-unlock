
# Genshin Impact FPS Unlocker

> **This is a fork of [34736384/genshin-fps-unlock](https://github.com/34736384/genshin-fps-unlock).**
> Fork source, builds and issue tracker: [AizawaHikaru233/genshin-fps-unlock](https://github.com/AizawaHikaru233/genshin-fps-unlock)
> Fork base: upstream `netcore` @ `2b85d61` (v3.5.0). Upstream is MIT licensed, © 2021-Present 34736384; see `LICENSE`.
> Everything below the fork sections is upstream documentation, kept as-is.
 - This tool helps you to unlock the 60 fps limit in the game
 - This is an external program which uses **WriteProcessMemory** to write the desired fps to the game
 - Handle protection bypass is already included
 - Does not require a driver for R/W access
 - Supports OS and CN version
 - Should work for future updates
 - If the source needs to be updated, I'll try to do it as soon as possible
 - You can download the compiled binary of this fork over at '[Releases](https://github.com/AizawaHikaru233/genshin-fps-unlock/releases)' if you don't want to compile it yourself
 - Free code signing provided by [SignPath.io](https://signpath.io/)
 ## Compiling
 - Use Visual Studio 2022 Community Edition to compile

 ### Cross Compiling on GNU/Linux
 You need [dotnet-8 SDK for Linux](https://dotnet.microsoft.com/en-us/download/dotnet) for .NET apps and x86_64-w64-mingw32 tool-chain for compiling the UnlockerStub to dll. Take Debian as an example, `apt install mingw-w64` installs the MinGW tool-chain. Finally, use `make` to build.

 ## Usage
 - Make sure you have the [.NET Desktop Runtime 8.0.0](https://dotnet.microsoft.com/en-us/download/dotnet/thank-you/runtime-desktop-8.0.0-windows-x64-installer) (Usually it should come installed)
 - Run the exe and click 'Start Game'
 - If it is your first time running, unlocker will attempt to find your game through the registry. If it fails, then it will ask you to either browse or run the game.
 - Place the compiled exe anywhere you want (except for the game folder)
 - Make sure your game is closed—the unlocker will automatically start the game for you
 - Run the exe as administrator, and leave the exe running
 >It requires adminstrator because the game needs to be started by the unlocker and the game requires such permission
 - To load other third-party plugins, go to `Options->Settings->DLLs` and click add

## Version 3.0.0 Changes
 - Rewritten the project in .NET 8
 - Added a launch option to use mobile UI (for streaming from mobile devices or touchscreen laptops)

## Fork Changes
### 3.6.0
 - New setting **Hide Unlocked Frame Rate** (`Settings -> Launch Options`, stored as `"FpsClamp"` in `fps_config.json`, on by default).
   The game is told 60 / 45 / 30 fps instead of the unlocked value, so the client-side frame rate check no longer
   sees an impossible number. The real frame rate is untouched — the game still renders at the value set in
   `FPSTarget`; only the value the client reads is clamped.
 - The clamp is applied by patching the game's frame rate getter in memory, resolved at run time: the stub locates
   the getter through its call sites, falls back to a data-flow scan for leaf functions reading the frame rate
   variable, and cross-checks the result against the variable the unlocker writes. No build-specific offsets are
   hard-coded, so a game update does not need a new build. If the getter cannot be identified with confidence the
   clamp is simply not applied (and a message says so) — the unlocker keeps working.
 - Turn the setting off to restore the previous behaviour; the detection then triggers as before.
 - Diagnostics: `%TEMP%\fpsclamp.log` records what was resolved and patched.
 - Version number of this fork starts at 3.6.0 and is shown in `About`.

## Notes
 - HoYoverse (miHoYo) is well aware of this tool, and you will not get banned for using **ONLY** fps unlock.
 - If you are using other third-party plugins, you are doing it at your own risk.
 - Any artifacts from unlocking fps (e.g. stuttering) is **NOT** a bug of the unlocker

# 原神解锁FPS限制

 - 工作原理类似于外部辅助，通过**WriteProcessMemory**把FPS数值写进游戏
 - 不需要通过驱动进行读写操作
 - 支持国服和外服
 - 理论上支持后续版本，不需要更新源码
 - 如果需要更新我会尽快更新

## 编译
 - 用VS2022编译

### GNU/Linux 上交叉编译
见 [Cross Compiling on GNU/Linux](#cross-compiling-on-gnulinux)

## 食用指南
 - 运行前确保系统已安装[.NET Desktop Runtime 8.0.0](https://dotnet.microsoft.com/en-us/download/dotnet/thank-you/runtime-desktop-8.0.0-windows-x64-installer) (一般来说系统自带就有)
 - 第一次运行的话先以管理员运行，解锁器会尝试通过注册表寻找游戏路经，如果找不到的话会提示你浏览游戏位置或者开启游戏
 - 解锁器放哪都行
 - 运行之前确保游戏是关闭的
 - 用管理员运行解锁器
 - 解锁器不能关掉
>使用管理员运行是因为游戏必须由解锁器启动，游戏本身就需要管理员权限了，所以负责启动的也是需要的

## 3.0.0 版本更新
 - 用.NET 8重写了项目
 - 添加了一个启动项，可以以移动端UI启动

## 本 fork 的改动

> 本仓库是 [34736384/genshin-fps-unlock](https://github.com/34736384/genshin-fps-unlock) 的 fork，基于 upstream `netcore` 分支 `2b85d61`（v3.5.0）。
> 源码、发布包与 issue 都在 [AizawaHikaru233/genshin-fps-unlock](https://github.com/AizawaHikaru233/genshin-fps-unlock)；上游采用 MIT 许可，版权归 34736384 所有，详见 `LICENSE`。

### 3.6.0
 - 新增「高帧防检测」开关（`设置 -> 启动选项`，对应 `fps_config.json` 的 `"FpsClamp"`，默认开启）：向游戏上报 60 / 45 / 30 三档帧率，
   使客户端侧的帧率检查读不到解锁后的异常数值。**实际帧率不受影响**，游戏仍按 `FPSTarget` 渲染，只有客户端读到的值被夹取。
 - 夹取通过修改游戏内存里的帧率 getter 实现，且定位在运行时完成：先按调用点签名找 getter，签名失效时改用数据流扫描
   （找读取同一帧率变量的叶子函数），再与解锁器写入的变量交叉校验。**不写死任何版本偏移**，游戏更新后不需要重新发版；
   若无法确信识别到 getter，则不打补丁（并提示原因），解锁功能照常工作。
 - 关掉该开关即恢复原行为，检测会照常触发。
 - 诊断日志：`%TEMP%\fpsclamp.log`，记录识别到哪条路径、打了哪个补丁。
 - 本 fork 版本号从 3.6.0 起，可在 `关于` 对话框查看。

