# 第四批设计：language / timezone 画像覆盖机制

状态：**设计中（本节随核实进度更新）**。产出为覆盖点设计定稿，不改集成
代码；Chromium 155 固定树只读研究。wire（LBPF1）已带 `language`
（可打印 ASCII，≤35）与 `timezone`（≤64）字段，但注入点此前因
WebPreferences 无语言字段而撤销（`reviews/build-b6.log`，
batch2 文档末节定论）。本文给出唯一推荐覆盖点与论证。

## 1. language：出口核实（文件:行号，155 固定树）

（待核实填充）

### 1.1 RendererPreferences 字段与 browser 侧填充链

### 1.2 renderer 侧消费：navigator.language(s) 取值链

### 1.3 网络侧：HTTP Accept-Language 头取值来源

## 2. language：机制选择论证

### 2.1 之前编译失败原因定位

### 2.2 唯一推荐覆盖点 + 备选

### 2.3 JS navigator.language(s) 与 HTTP 头是否同源同覆盖

## 3. timezone 梳理

### 3.1 出口核实（blink::TimeZoneController / SetTimeZoneOverride / ICU）

### 3.2 推荐 browser 侧覆盖点

## 4. wire 字段 → 注入映射

## 5. 测试先行计划

### 5.1 bootstrap_unittests 用例

### 5.2 真浏览器验证矩阵

## 6. 明确不做清单
