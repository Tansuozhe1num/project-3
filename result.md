# MIT 6.172 Project 3 实验报告

## 1. 实验目标

根据 `README.txt` 的要求，本实验需要完成一个可工作的自定义内存分配器，并补全堆有效性验证器，最终通过 `mdriver` 对正确性、空间利用率和吞吐量进行测试。

本次实验的交付物包括：

- 完成 `mymalloc/allocator.c` 中的分配器实现
- 完成 `mymalloc/validator.h` 中的验证逻辑
- 运行测试并记录命令、结果和性能指标

---

## 2. 实验环境

- 主机系统：Windows
- 构建环境：WSL2 Ubuntu
- 项目目录：`/mnt/d/temp/pessproject3/mymalloc`
- 编译器：`clang`

说明：项目原始构建脚本依赖类 Unix 工具链，Windows PowerShell 环境中缺少 `make` 以及若干 POSIX 头文件，因此本实验采用 WSL 执行编译与测试命令，以保持与原实验环境一致。

---

## 3. 初始代码分析

### 3.1 初始分配器存在的问题

`allocator.c` 初始版本只是一个最简单的 bump-pointer 分配器，主要问题如下：

1. `malloc` 每次都调用 `mem_sbrk` 扩展堆，没有空闲块复用
2. `free` 完全为空实现，释放后空间无法回收
3. `realloc` 只能“重新申请 + memcpy + free”，缺乏就地扩展能力
4. `my_check` 只检查线性头部，不足以验证空闲链表和块一致性

这意味着初始版本虽然结构简单，但空间利用率会非常差，不满足实验要求。

### 3.2 初始验证器存在的问题

`validator.h` 中多个关键位置仍是 `TODO`：

- 未检查返回地址的对齐
- 未检查 payload 是否越界
- 未检查 payload 是否与已有块重叠
- 未实现 range 删除
- 未验证 `realloc` 是否保留旧数据

因此必须先补全 validator，才能可靠评估 allocator 的正确性。

---

## 4. 实验步骤与修改过程

## 步骤一：阅读项目说明并确认评分方式

### 执行命令

```bash
cd /mnt/d/temp/pessproject3/mymalloc
```

### 关键结论

根据 `README.txt`，评分由两部分组成：

- 空间利用率 `utilization`
- 吞吐量 `throughput`

最终分数是二者的几何平均，`UTIL_WEIGHT = 0.50`，因此本实验不能只追求速度，也不能只追求节省空间，必须兼顾两者。

### 本步骤采用的优化思路

- 先确定评分公式，再选数据结构，避免只做“能跑”的朴素实现
- 由于既要回收空间又要保证速度，最终选择“显式空闲链表 + 分离适配”的方案

---

## 步骤二：补全 validator，建立正确性保障

### 执行命令

```bash
cd /mnt/d/temp/pessproject3
```

### 修改文件

- `mymalloc/validator.h`

### 关键代码

```c
if (!IS_ALIGNED(lo)) {
  sprintf(msg, "Payload address (%p) is not aligned to %d bytes", lo, R_ALIGNMENT);
  malloc_error(tracenum, opnum, msg);
  return 0;
}

if (lo < (char*)impl->heap_lo() || hi > (char*)impl->heap_hi()) {
  sprintf(msg, "Payload (%p:%p) lies outside heap (%p:%p)",
          lo, hi, impl->heap_lo(), impl->heap_hi());
  malloc_error(tracenum, opnum, msg);
  return 0;
}

for (p = *ranges; p != NULL; p = p->next) {
  if (!(hi < p->lo || lo > p->hi)) {
    sprintf(msg, "Payload (%p:%p) overlaps another payload (%p:%p)",
            lo, hi, p->lo, p->hi);
    malloc_error(tracenum, opnum, msg);
    return 0;
  }
}
```

```c
for (int j = 0; j < oldsize; j++) {
  if (newp[j] != (char)(index ^ j)) {
    malloc_error(tracenum, i, "realloc did not preserve the old payload");
    return 0;
  }
}
```

### 本步骤采用的优化思路

1. **先做验证器，再做优化器**
   - 这样后续每次改 allocator 时，都能快速发现对齐错误、越界错误和重叠错误
   - 避免性能调优过程中引入隐蔽 bug

2. **使用 range 链表追踪已分配区间**
   - 每次 `malloc/realloc` 后把 payload 区间加入链表
   - 每次 `free` 时删除对应区间
   - 这样可以稳定检查块重叠问题

3. **为 realloc 写入可验证数据模式**
   - 申请后向 payload 写入 `index ^ j`
   - 重新分配后检查旧内容是否被正确保留
   - 这样可以验证 `realloc` 的语义正确性，而不是只验证返回指针非空

### 效果

validator 完成后，可以对坏分配器进行自动报错，这为后续 allocator 调试提供了可靠基础。

---

## 步骤三：实现块格式与堆基础结构

### 执行命令

```bash
cd /mnt/d/temp/pessproject3/mymalloc
```

### 修改文件

- `mymalloc/allocator.c`

### 关键代码

```c
#define WSIZE SIZE_T_SIZE
#define DSIZE (2 * WSIZE)
#define CHUNKSIZE (1 << 12)
#define LISTS 16

#define OVERHEAD (2 * WSIZE)
#define MIN_BLOCK_SIZE (ALIGN(OVERHEAD + 2 * sizeof(void*)))

#define HDRP(bp) ((char*)(bp) - WSIZE)
#define FTRP(bp) ((char*)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)
#define NEXT_BLKP(bp) ((char*)(bp) + GET_SIZE(HDRP(bp)))
#define PREV_BLKP(bp) ((char*)(bp) - GET_SIZE(((char*)(bp) - DSIZE)))
```

### 设计说明

本实验采用带头尾边界标记的块布局：

- 头部：记录块大小和分配位
- 尾部：记录块大小和分配位
- 空闲块 payload：存放双向链表指针

### 本步骤采用的优化思路

1. **使用边界标记**
   - 可以 O(1) 找到前后相邻块
   - 为后续的快速合并空闲块打基础

2. **设置最小块大小**
   - 确保空闲块至少能容纳头部、尾部和前后链指针
   - 避免切分后产生不可管理的小碎片

3. **一次性定义统一宏**
   - 减少指针运算错误
   - 让 `malloc/free/realloc/check` 共享同一套块解释逻辑

---

## 步骤四：实现分离空闲链表

### 关键代码

```c
static void* free_lists[LISTS];

static int list_index(size_t size) {
  int index = 0;
  size_t bound = MIN_BLOCK_SIZE;
  while (index < LISTS - 1 && size > bound) {
    bound <<= 1;
    index++;
  }
  return index;
}
```

```c
static void insert_free_block(void* bp) {
  int index = list_index(GET_SIZE(HDRP(bp)));
  void* head = free_lists[index];

  NEXT_FREEP(bp) = head;
  PREV_FREEP(bp) = NULL;
  if (head != NULL) {
    PREV_FREEP(head) = bp;
  }
  free_lists[index] = bp;
}
```

### 本步骤采用的优化思路

1. **采用分离适配而不是单一链表**
   - 小块和大块分开管理
   - 查找合适空闲块时遍历范围更小，吞吐量更高

2. **使用双向链表**
   - 删除任意空闲块时可直接 O(1) 摘除
   - 避免单链表删除时必须线性找前驱

3. **按大小区间分桶**
   - 减少“拿很大的空闲块分配很小请求”的情况
   - 对空间利用率和查找速度都有帮助

### 效果

这一步是本次实验最关键的吞吐量优化来源。相比每次线性扫描整个堆，分离链表明显降低了查找成本。

---

## 步骤五：实现 free 的立即合并与 malloc 的块分裂

### 关键代码

```c
static void* coalesce(void* bp) {
  size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
  size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
  size_t size = GET_SIZE(HDRP(bp));
  ...
}
```

```c
static void place(void* bp, size_t asize) {
  size_t csize = GET_SIZE(HDRP(bp));
  remove_free_block(bp);

  if (csize - asize >= MIN_BLOCK_SIZE) {
    PUT(HDRP(bp), PACK(asize, 1));
    PUT(FTRP(bp), PACK(asize, 1));

    bp = NEXT_BLKP(bp);
    PUT(HDRP(bp), PACK(csize - asize, 0));
    PUT(FTRP(bp), PACK(csize - asize, 0));
    insert_free_block(bp);
    return;
  }

  PUT(HDRP(bp), PACK(csize, 1));
  PUT(FTRP(bp), PACK(csize, 1));
}
```

### 本步骤采用的优化思路

1. **立即合并相邻空闲块**
   - `free` 后立刻与前后空闲块合并
   - 减少外部碎片，提升后续大块申请成功率

2. **按需分裂空闲块**
   - 若空闲块明显大于请求大小，则拆成“已分配块 + 剩余空闲块”
   - 避免浪费整块大内存，提升空间利用率

3. **限制最小剩余块**
   - 只有剩余空间足够形成合法空闲块时才分裂
   - 避免生成无法复用的碎片块

### 效果

这一阶段直接改善了默认 traces 中的空间利用率，是得分提升的核心步骤之一。

---

## 步骤六：实现 `realloc` 的就地扩展优化

### 关键代码

```c
if (asize <= oldsize) {
  if (oldsize - asize >= MIN_BLOCK_SIZE) {
    PUT(HDRP(ptr), PACK(asize, 1));
    PUT(FTRP(ptr), PACK(asize, 1));
    next_bp = NEXT_BLKP(ptr);
    PUT(HDRP(next_bp), PACK(oldsize - asize, 0));
    PUT(FTRP(next_bp), PACK(oldsize - asize, 0));
    coalesce(next_bp);
  }
  return ptr;
}

next_bp = NEXT_BLKP(ptr);
if (!GET_ALLOC(HDRP(next_bp))) {
  next_size = GET_SIZE(HDRP(next_bp));
  if (oldsize + next_size >= asize) {
    ...
    return ptr;
  }
}
```

### 本步骤采用的优化思路

1. **缩容时原地完成**
   - 当新大小更小时，不重新申请内存
   - 直接把尾部切成空闲块并合并，减少拷贝开销

2. **优先尝试向后扩展**
   - 如果当前块后面正好是空闲块，就直接并入
   - 避免不必要的 `malloc + memcpy + free`

3. **只在必要时才复制**
   - 只有在无法就地扩展时才申请新块
   - 这一点对 realloc 较多的 trace 很重要

### 效果

该优化主要改善吞吐量，并减少 `realloc` 场景下的瞬时额外内存占用。

---

## 步骤七：实现堆一致性检查 `my_check`

### 关键代码

```c
if (GET(HDRP(bp)) != GET(FTRP(bp))) {
  printf("Header/footer mismatch at %p.\\n", bp);
  return -1;
}

if (!GET_ALLOC(HDRP(bp))) {
  free_blocks_heap++;
  if (!GET_ALLOC(HDRP(NEXT_BLKP(bp))) && GET_SIZE(HDRP(NEXT_BLKP(bp))) > 0) {
    printf("Uncoalesced neighbors at %p.\\n", bp);
    return -1;
  }
}
```

### 本步骤采用的优化思路

1. **检查头尾一致性**
   - 能快速定位写坏 header/footer 的问题

2. **检查未合并相邻空闲块**
   - 保证 coalesce 逻辑确实生效

3. **检查空闲链表中的块是否合法**
   - 防止“已分配块误入空闲链表”或“桶分类错误”

### 效果

这一步增强了 allocator 自检能力，与 validator 配合后，调试效率明显提高。

---

## 5. 编译与测试命令

### 5.1 编译

```bash
wsl bash -lc "cd /mnt/d/temp/pessproject3/mymalloc && make clean all"
```

### 5.2 运行默认 traces

```bash
wsl bash -lc "cd /mnt/d/temp/pessproject3/mymalloc && ./mdriver -c -v"
```

### 5.3 验证 validator 能否抓到坏分配器

```bash
wsl bash -lc "cd /mnt/d/temp/pessproject3/mymalloc && ./mdriver -b -c"
```

### 5.4 运行 additional_traces

```bash
wsl bash -lc "cd /mnt/d/temp/pessproject3/mymalloc && ./mdriver -t additional_traces -c -v"
```

### 5.5 运行分配器压力测试

```bash
wsl bash -lc "cd /mnt/d/temp/pessproject3/mymalloc && ./allocator_test"
```

---

## 6. 实验结果

## 6.1 默认 traces 结果

核心输出如下：

```text
# GeometricMean(79.575657 (util),  96.374307 (tput))  =  87.573105
```

### 结果解读

- 几何平均空间利用率：`79.575657%`
- 几何平均吞吐量得分：`96.374307%`
- 最终综合得分：`87.573105`

### 默认 traces 中的观察

1. 多数 trace 的吞吐量都达到或超过基准上限，说明分离空闲链表有效降低了查找开销
2. `trace_c3_v0` 和 `trace_c9_v0` 的空间利用率分别只有 `59%` 和 `40%`
3. 这说明当前实现虽然整体均衡，但在特定工作负载下仍有碎片问题

---

## 6.2 additional_traces 结果

核心输出如下：

```text
# GeometricMean(77.862195 (util),  100.000000 (tput))  =  88.239558
```

### 结果解读

- 几何平均空间利用率：`77.862195%`
- 几何平均吞吐量得分：`100.000000%`
- 最终综合得分：`88.239558`

### 泛化分析

1. 在额外 trace 集合上仍然全部通过正确性检查
2. 吞吐量得分达到满分，说明该实现对不同 trace 具有较好的速度稳定性
3. 空间利用率略低于默认 traces，表明某些变体对碎片更敏感，但整体仍保持较好表现

---

## 6.3 validator 验证结果

执行坏分配器测试后，validator 成功识别出如下问题：

- 返回地址未按 8 字节对齐
- payload 超出堆边界
- 分配失败

示例报错：

```text
ERROR [trace 0, line 6]: Payload address (0x74c38f600015) is not aligned to 8 bytes
ERROR [trace 2, line 5]: Payload (0x74c38f5ff010:0x74c38f6005db) lies outside heap ...
```

这说明 `validator.h` 中补全的对齐、边界和区间检查逻辑工作正常。

---

## 6.4 allocator_test 结果

输出如下：

```text
total runtime: 0.180657s
total mem usage: 135280 bytes
```

说明该 allocator 能稳定完成重复的大量分配/释放操作，没有出现崩溃或明显异常。

---

## 7. 最终实现总结

本实验最终完成了一个具备以下特性的内存分配器：

1. **显式分离空闲链表**
   - 提升查找速度
   - 降低全堆扫描开销

2. **边界标记 + 立即合并**
   - 快速合并相邻空闲块
   - 降低外部碎片

3. **按需分裂**
   - 减少大块浪费
   - 提高空间利用率

4. **realloc 原地优化**
   - 缩小块时原地拆分
   - 扩容时优先尝试并入后继空闲块

5. **完整 validator + heap checker**
   - 能检查对齐、越界、重叠、realloc 数据保留
   - 能检查空闲链表与堆结构的一致性

---

## 8. 仍可继续优化的方向

虽然当前结果已经较好，但仍有进一步优化空间：

1. **减少已分配块尾部 footer 开销**
   - 当前所有块都保留 footer，虽然实现简单，但会损失部分利用率
   - 若改成“仅空闲块保留 footer + prev_alloc 位”，有望提高 `trace_c3/c9` 的利用率

2. **更精细的分桶策略**
   - 目前桶划分较粗
   - 可根据 trace 分布进一步细化小块区间

3. **更激进的 realloc 扩容策略**
   - 如在堆尾时尝试直接扩堆并原地增长
   - 可以继续减少复制次数

---

## 9. 结论

本次实验已按要求完成：

- 成功实现了可工作的自定义内存分配器
- 成功补全并验证了 heap validator
- 默认 traces 综合得分为 **87.573105**
- additional_traces 综合得分为 **88.239558**

整体来看，该实现已经在正确性、吞吐量和空间利用率之间取得了较好的平衡，满足本实验要求。
