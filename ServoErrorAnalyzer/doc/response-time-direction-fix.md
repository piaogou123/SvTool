# 响应时间算法：fb 缓慢换向检测修复

## 问题背景

伺服响应时间分析算法的核心逻辑是：对于每一个 cmd（指令位置）采样点 i，向前搜索 fb（反馈位置）采样序列，找到 fb[j] 最接近 cmd[i] 的时刻，响应时间 = time[j] - time[i]。

为避免在 fb 与 cmd 运动方向相反时错误匹配，算法会先等待 fb 换向到与 cmd 同向，再从同向窗口内寻找最接近值。

## 问题现象

**数据文件：** Dump 260521 163214.csv
**起始时间：** 8.860s，X 轴
**cmd 方向：** 上升（dir = 1）
**target（cmd[i]）：** 1015.239358 mm
**预期响应时间：** ~46ms（fb 在上升沿穿越 target 的时刻约 8.906s）

但修复前算法输出的响应时间为 202ms，结束时间错误地落到 9.062s（fb 下降沿再次穿越 target 的位置）。

### 数据追踪

```
时间       X_fb          |fb - target|  状态
---------- ------------  ------------  ------
8.860      1014.141440   1.097918      起始点，fb < target，上升中
8.906      1015.236735   0.002623      ← 上升沿首次穿越 target（最佳匹配）
8.907      1015.244365   0.005007      继续上升越过 target
...
8.955      1015.370369   0.131011      ← fb 达到峰值，开始换向
8.956      1015.370369   0.131011      平台期
...
9.062      1015.239239   0.000119      ← 下降沿再次穿越 target（错误匹配点）
```

fb 在 8.906s 上升沿已经穿越 target（diff = 0.002623），这是最合理的匹配点。但 fb 继续冲顶到 1015.370，然后缓慢回落，在 9.062s 再次穿越 target 并得到更小的差值（diff = 0.000119）。算法错误地选择了下降沿上的点。

## 根因分析

修复前，算法使用两层机制控制搜索范围：

### 机制 1：per-sample 方向过滤（localDirection）

```cpp
QVector<int> cmdDir = localDirection(cmd, eps, 2);
QVector<int> fbDir  = localDirection(fb, eps, 2);
```

`localDirection` 用 2 样本窗口的简单斜率判断方向：

```cpp
double diff = signal[i] - signal[i - window];  // window = 2
if (diff > eps)  dir = 1;    // 上升
else if (diff < -eps) dir = -1;  // 下降
else dir = 0;                   // 静止/噪声
```

其中 `eps = max(cmdRange * 0.0001, 0.005) ≈ 0.005 mm`。

### 机制 2：搜索循环中的方向检查

```cpp
if (cmdDir[i] != 0 && fbDir[j] != 0 && fbDir[j] != cmdDir[i]) {
    if (foundAny) break;   // 已找到同向候选，换向则结束
    continue;              // 还没找到同向候选，继续等待换向
}
```

当 `fbDir[j] != cmdDir[i]` 时，说明 fb 运动方向与 cmd 不一致，跳过该点。

### 失效原因

**问题出在 `eps = 0.005`** 对于 fb 的缓慢换向来说太大了。

在本例中，fb 从峰值 1015.370 回落的速率约为 0.0012 mm/sample。2 样本窗口的差异最大仅为 ~0.002 mm，远小于 eps = 0.005 mm。因此：

- fb 在峰值附近：`fb[j] - fb[j-2] ≈ -0.0002`，绝对值 < 0.005 → **fbDir = 0**
- fb 在回落过程中：`fb[j] - fb[j-2] ≈ -0.001`，绝对值 < 0.005 → **fbDir = 0**
- 直到 fb 回落到变化累积超过 0.005 时才会变成 fbDir = -1

在 fb 换向后的漫长回落过程中，`fbDir[j]` 始终为 0。机制 2 的检查条件 `fbDir[j] != 0 && fbDir[j] != cmdDir[i]` 中 `fbDir[j] != 0` 为 false，方向过滤完全不触发。搜索窗口一直延伸到 `maxAhead` 边界（500 个样本），fb 在下降沿再次穿越 target 时反而得到更小的数值差值，算法选择了错误的时间点。

### 问题总结

| 机制 | 能检测 | 不能检测 |
|------|--------|---------|
| 快速换向（per-step 变化 > 0.005mm） | 能，fbDir 立刻翻转 | — |
| 缓慢换向（per-step 变化 < 0.005mm） | — | fbDir 恒为 0，检测不到换向 |

## 修复方案

### 增加累积回退检测（cumulative retreat tracking）

在原有 per-sample 方向过滤之上，增加一层**基于运行极值的累积回退检测**：

```cpp
int dir = cmdDir[i];
bool tracking = false;
double fbExt = 0;

for (int j = i + 1; j < jEnd; ++j) {
    // 原有机制：per-sample 方向过滤（快速换向）
    if (dir != 0 && fbDir[j] != 0 && fbDir[j] != dir) {
        if (foundAny) break;
        continue;
    }
    foundAny = true;

    // 新增机制：累积回退检测（缓慢换向）
    if (dir != 0) {
        if (!tracking) {
            tracking = true;
            fbExt = fb[j];
        } else if (dir == 1) {
            if (fb[j] > fbExt)
                fbExt = fb[j];           // 更新运行最大值
            else if (fbExt - fb[j] > eps)
                break;                   // 从峰值回退超阈值 → 换向
        } else {  // dir == -1
            if (fb[j] < fbExt)
                fbExt = fb[j];           // 更新运行最小值
            else if (fb[j] - fbExt > eps)
                break;                   // 从谷值回升超阈值 → 换向
        }
    }

    // 正常的最接近值搜索
    double diff = std::abs(fb[j] - target);
    if (diff < bestDiff) {
        bestDiff = diff;
        bestJ = j;
    }
}
```

### 原理

跟踪 fb 在搜索方向上的运行极值（cmd 上升时跟踪最大值，cmd 下降时跟踪最小值）。当 fb 从极值位置回退超过 `eps` 时，判定 fb 已经换向，立即结束搜索。

关键区别：

- **per-sample 方向过滤**看的是相邻 2 个样本的局部斜率，要求 `|fb[j] - fb[j-2]| > eps` 才能判断方向。缓慢变化时局部斜率始终低于阈值，输出 dir=0。
- **累积回退检测**看的是从历史极值的累计回退量 `|fbExt - fb[j]|`。只要 fb 从极值位置持续单方向移动，无论每步多小，累积量最终会超过 eps，从而检测到换向。

### 修复后追踪（同一起始点 8.860s）

```
时间       X_fb          状态
---------- ------------  ------
8.860      1014.141440   起始点，dir=1, target=1015.239
8.861      1014.188170   fbDir=1, tracking 开始, fbExt=1014.188
8.862      1014.233589   fbExt 更新 → 1014.234
...                      fbExt 持续跟进上升
8.906      1015.236735   ← 上升沿穿越 target, bestDiff=0.002623 (bestJ)
...                      fbExt 继续跟进
8.955      1015.370369   ← fbExt 达到最终峰值
8.956      1015.370369   平台期, fbDir=0 (变化太小)
8.961      1015.370250   fbExt-fb=0.000119 < 0.005, 继续
8.978      1015.365124   fbExt-fb=0.005245 > 0.005 → 检测到换向, break!
```

搜索在 8.978s 中断，bestJ 停留在上升沿的最佳匹配 8.906s。

## 修复效果

| 参数 | 修复前（错误） | 修复后（正确） |
|------|--------------|--------------|
| 结束时间 | 9.062 s | **8.906 s** |
| 结束位置 | 1015.239239 mm | **1015.236735 mm** |
| 响应时间 | 202 ms | **46 ms** |

## 影响范围

修复仅改变 `computeOneAxis()` 内部的搜索循环逻辑（[data_loader.cpp:275-319](ServoErrorAnalyzer/data_loader.cpp#L275-L319)），不影响其他函数和对外接口。两种换向检测机制互补：

1. **per-sample 方向过滤**：处理快速换向（> 0.005mm/2samples），在换向发生后立即中断搜索
2. **累积回退检测（新增）**：处理缓慢换向（< 0.005mm/2samples），在累计回退超过阈值后中断搜索

---

## 补充修复：cmdDir 为 0 时方向检测被静默跳过

### 问题现象

**起始时间：** 8.886s，X 轴
**target（cmd[i]）：** 1015.356183 mm
**预期响应时间：** ~49ms（fb 在上升沿穿越 target 的时刻 8.935s）

但修复后算法输出响应时间为 101ms，结束时间落到 8.987s（fb 下降沿再次穿越 target）。

### 数据追踪

```
时间       X_fb          |fb - target|  状态
---------- ------------  ------------  ------
8.886      1014.978051   0.378132      起始点，fb < target，上升中
8.934      1015.355110   0.001073      上升沿逼近 target
8.935      1015.356660   0.000477      ← 上升沿首次穿越 target（正确匹配）
8.936      1015.357971   0.001788      继续上升越过 target
...
8.955      1015.370369   0.014186      fb 达到峰值，开始换向
...
8.986      1015.357375   0.001192      下降沿逼近 target
8.987      1015.356064   0.000119      ← 下降沿再次穿越 target（错误匹配，差值更小）
```

### 根因分析

在第一次修复中，所有的方向约束都受 `dir != 0` 条件控制：

```cpp
int dir = cmdDir[i];   // 从 cmd 的 2 样本斜率获取方向

// per-sample 方向过滤
if (dir != 0 && fbDir[j] != 0 && fbDir[j] != dir) { ... }

// 累积回退检测
if (dir != 0) { ... }
```

当 cmd 运动非常缓慢时，`localDirection(cmd, eps=0.005, window=2)` 输出的 `cmdDir[i] = 0`：

```
cmd[i-2] at 8.884: 1015.352368
cmd[i]   at 8.886: 1015.356183
diff = 1015.356183 - 1015.352368 = 0.003815 < 0.005 → cmdDir = 0
```

尽管 cmd 实际上在持续上升（6 样本累积上升 0.013mm），但 2 样本窗口内的变化量低于 eps 阈值，导致 `dir = 0`。

此时 `dir == 0`，两个 `if (dir != 0)` 条件均为 false，**所有方向约束被静默跳过**，搜索退化为无约束的纯数值最近值匹配。算法再次穿过换向点，在下陷沿上找到更接近的数值。

**与第一个 bug 的对比：**

| | Bug 1 (8.860s) | Bug 2 (8.886s) |
|---|---|---|
| cmdDir[i] | 1（可检测） | 0（变化太小） |
| fb 换向 | 缓慢（fbDir 无法检测） | 缓慢（fbDir 无法检测） |
| 失效环节 | 累积回退检测不存在 | `dir == 0` 导致累积回退被跳过 |
| 修复 | 增加累积回退检测 | dir fallback 到 fbDir |

两个 bug 都导致算法穿过 fb 换向点选到下降沿上的错误匹配，但触发路径不同：第一个是累积回退机制缺失，第二个是 `dir == 0` 绕过了已添加的累积回退机制。

### 修复方案

当 `cmdDir[i] == 0` 时，fallback 到 `fbDir[i]` 作为搜索方向：

```cpp
int dir = cmdDir[i];
// When cmd is moving too slowly for per-sample direction detection,
// fall back to fb's direction so the search is still constrained.
if (dir == 0)
    dir = fbDir[i];
```

此时 fb 的运动方向成为搜索的约束方向：
- per-sample 方向过滤生效：`fbDir[j]` 与 `dir` 不一致时跳过或中断
- 累积回退检测生效：跟踪 fb 运行极值，检测缓慢换向

对于 8.886s 的例子：`cmdDir=0`, `fbDir=1` → `dir=1`，fb 上升穿越 target 后被正确识别，上升沿最佳匹配保留，fb 换向后触发累积回退中断搜索。

### 修复后追踪

```
时间       X_fb          状态
---------- ------------  ------
8.886      1014.978051   起始点，cmdDir=0, fbDir=1 → dir=1 (fallback)
8.887      1014.997125   fbDir=1, tracking 开始, fbExt=1014.997
...                      fbExt 持续跟进上升
8.935      1015.356660   ← 上升沿穿越 target, bestDiff=0.000477 (bestJ)
...                      fbExt 继续跟进
8.955      1015.370369   ← fbExt 达到最终峰值
8.956      1015.370369   平台期, fbDir=0 (变化太小)
8.978      1015.365124   fbExt-fb=0.005245 > 0.005 → 检测到换向, break!
```

搜索在 8.978s 中断，bestJ 停留在上升沿最佳匹配 8.935s。

### 修复效果

| 参数 | 修复前 | 修复后 |
|------|--------|--------|
| 结束时间 | 8.987 s | **8.935 s** |
| 结束位置 | 1015.356064 mm | **1015.356660 mm** |
| 响应时间 | 101 ms | **49 ms** |
