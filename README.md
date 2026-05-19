# ServoErrorAnalyzer

伺服轴误差分析工具，读取 CSV 数据文件，可视化各轴的位置误差并计算响应延迟统计。

---

## 功能

- 加载伺服数据 CSV 文件（支持拖拽）
- 多轴误差曲线实时显示（X / Y / C / A / Z …）
- 鼠标游标：悬停查看任意时刻的误差值
- 框选缩放 / 右键复位 / Ctrl+滚轮缩放
- 右侧面板显示各轴响应时间统计（均值、最大、最小、中位数、标准差）

---

## CSV 格式

支持两种格式：

**标准格式**（推荐）— 列名符合 `axes.mach.<l|f>.p[<轴名>]` 规则：

```
TIME, axes.mach.l.p[X], axes.mach.f.p[X], axes.mach.l.p[Y], axes.mach.f.p[Y], ...
```

**兼容格式** — 固定 8 列 X/Y/C 结构：

```
FINISHED, time, xCmd, yCmd, cCmd, xFb, yFb, cFb
```

第二行可选填写单位（以 `units,` 或 `units\t` 开头），会自动跳过。

---

## 构建

依赖：Qt 5 或 Qt 6（含 Widgets 模块）

```bash
cd ServoErrorAnalyzer
qmake ServoErrorAnalyzer.pro
make
```

或用 CMake：

```bash
cmake -B build
cmake --build build
```

---

## 操作说明

| 操作 | 效果 |
|------|------|
| 拖拽 CSV 到窗口 | 加载文件 |
| 鼠标悬停图表 | 显示游标及误差值 |
| 左键框选 | 放大选区 |
| 右键单击 | 恢复全图 |
| Ctrl + 滚轮 | 以鼠标为中心缩放 |
| 工具栏复选框 | 显示/隐藏各轴 |

---

## 已知问题

- `kPalette` 颜色表在 `chart_view.cpp` 和 `main_window.cpp` 中各定义一份，存在重复
- `dataStartLine` 变量计算后未被使用（死代码）
- 响应时间中位数计算使用完整排序，可改用 `std::nth_element` 优化
