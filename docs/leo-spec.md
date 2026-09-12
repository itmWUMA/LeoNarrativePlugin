# .leo 剧本语言规范 v0.1

> 本文件是 `.leo` 语言的**唯一权威规范**。编译器（`Source/LeoNarrative/Public/Script/`）、
> AI 写作技能（`.zcode/skills/leo-script/SKILL.md`）与黄金语料（`tests/golden/`）
> 三者都必须与本文件保持一致；改动语言必须先改这里。

## 1. 设计总则

- **AI 优先创作**：语法面向 LLM 与程序员，一行一命令，无嵌套括号结构。
- **文本是唯一事实源**：`.leo` 源文件即剧本本体；编译产物（命令列表）是 Transient 派生物，永不落盘。
- **载入时编译、运行时解释**：编辑期拿到全部错误（带行号），运行期零解析开销。
- **有界表达力**：无循环、无函数、无用户自定义控制流。复杂分支交给 ScenarioGraph 编排。
- v0.1 表达式刻意收窄：字面量、黑板引用、比较、逻辑、四则。无函数调用、无属性访问。

## 2. 文件与字符

| 规则 | 说明 |
|---|---|
| 编码 | UTF-8（有无 BOM 均可） |
| 换行 | LF 或 CRLF |
| **Tab** | **任何位置（含注释、字符串）出现 `\t` 都是硬错误 `E_TAB`**。缩进与文本一律用空格 |
| 空行 | 允许，忽略 |
| 注释 | `#` 必须是行内**第一个非空字符**才视为注释；行中 `#` 不是注释（文本正文可能含 `#`） |
| 行尾 | 每行右侧尾随空格被去除后参与解析 |

## 3. 词法结构

命令行先按空白切成 token（`text` 与 choice 选项行的正文部分除外，见 §4/§5）：

| Token | 定义 |
|---|---|
| Word | 连续的不含下列字符的序列：空格 `"` `=` `<` `>` `!` `&` `|` `+` `-` `*` `/` `%` `(` `)` |
| Int | `[0-9]+`（无符号；负数用一元 `-` 表达） |
| Float | `[0-9]+ . [0-9]+` |
| String | `"..."`，支持转义 `\"` `\\` `\n` `\t` `\r`；其余转义报 `E_BAD_ESCAPE`；未闭合报 `E_UNTERM_STRING` |
| Op | `==  !=  <=  >=  &&  \|\|  !  <  >  +  -  *  /  %  (  )  =  +=  -=  *=  /=` |
| Arrow | `->`（`-` 紧跟 `>`） |

**标识符**（label / 变量 / 逻辑资源名 / 选项目标）：`[A-Za-z_][A-Za-z0-9_]*`，仅 ASCII。
**宽松 Word**（speaker、char 槽位名）：任意 Word token，允许 CJK。
词法上 Word 允许 CJK 等多字节字符，但只有标识符语境（变量、label、逻辑名）会做严格校验。

## 4. 行与缩进

- 顶层命令从第 0 列开始。
- 缩进只用空格，每级 **4 个**。缩进数非 4 的倍数 → `E_INDENT`。
- v0.1 只有一个块结构：`choice`。其选项行缩进恰为 1 级（4 空格）。
- 缩进超过 1 级 → `E_INDENT_LEVEL`；`choice` 内出现非选项行 → `E_ARG_BAD`（提示"choice 块内只允许选项行"）。
- 空行与注释行**不参与**缩进判定。

## 5. 命令全表（v0.1）

通用规则：位置参数在前，`key=value` 命名参数在后；命令未声明的命名参数 → `E_BAD_PARAM`；
位置参数个数不符 → `E_ARG_COUNT`。

| # | 命令 | 语法 | 阻塞 | 说明 |
|---|---|---|---|---|
| 1 | `label` | `label <name>` | 否 | 跳转锚点。name 为标识符；重复定义 → `E_DUP_LABEL` |
| 2 | `text` | `text [<speaker> \| ]<body>` | **等点击** | 见下方专项规则 |
| 3 | `bg` | `bg <id>` | 否 | 背景。参数 `transition=<word>`（默认 `cut`）、`duration=<float秒>`（默认 0.5） |
| 4 | `char` | `char <slot> <id或->` | 否 | 立绘。`-` 表示移除该槽位。参数 `at=left\|center\|right`、`pose=<word>`、`motion=<word>` |
| 5 | `bgm` | `bgm <id或->` | 否 | 循环 BGM；`-` 停止。参数 `volume=<0..1>`、`fade=<float秒>` |
| 6 | `se` | `se <id>` | 否 | 一次性音效。参数 `volume=<0..1>` |
| 7 | `voice` | `voice <id>` | 否 | 语音，通常紧邻其 `text` 行之前 |
| 8 | `wait` | `wait <ms>` | **等计时** | 整数字面量，0..600000，否则 `E_ARG_BAD` |
| 9 | `jump` | `jump <label>` | 否 | 无条件跳转。目标不存在 → `E_UNDEF_LABEL` |
| 10 | `jumpif` | `jumpif <expr> -> <label>` | 否 | 条件跳转。以行内**第一个** ` -> ` 切分表达式与目标 |
| 11 | `set` | `set <name> <op> <expr>` | 否 | 写**局部**作用域。op ∈ `= += -= *= /=` |
| 12 | `setg` | `setg <name> <op> <expr>` | 否 | 写**全局**作用域（跨章节存活，进全局档） |
| 13 | `choice` | `choice` + 缩进选项行 | **等选择** | 见下方专项规则 |
| 14 | `end` | `end` | 终止 | 该执行路径终止。全文件至少一个 `end`（`E_MISSING_END`）；**允许多个**（多分支章节各自提前收束，v0.11 修订） |

### 5.1 `text` 专项规则

```
text 李雷 | 你好，世界。 今天天气不错。
text - | 一段旁白……
text 直接就是旁白也可以。
```

- `text` 后的第一个 ` | `（空格+竖线+空格）切分 speaker 与 body；无分隔符则整行是 body、speaker 为空。
- speaker 为 `-` 表示旁白，内部规范化为空字符串。
- body 为**行内原文**（rest-of-line），不做 token 切分，可含任意字符（Tab 除外，见 §2）。
- body 去除尾随空白后不得为空，否则 `E_ARG_BAD`。

### 5.2 `choice` 专项规则

```
choice
    去图书馆 -> branch_library
    直接回家 -> branch_home
    追上去 -> branch_chase if affection >= 5
```

- 选项行：`<显示文本> -> <label> [if <expr>]`，以行内**最后一个** ` -> ` 切分文本与目标
  （显示文本自身含 ` -> ` 的歧义由此消解）。
- 目标 label 必须存在（`E_UNDEF_LABEL`）。`if` 条件为假时该选项不展示。
- 选项数 1..8；空 choice 块 → `E_EMPTY_CHOICE`；choice 嵌套 choice → `E_NESTED_CHOICE`。
- 玩家选择后，**框架把结果写入黑板** `last_choice`（局部作用域，Int，0 起），再由脚本 `jumpif` 分流；
  VM 不直接改写执行指针——这是架构铁律。

## 6. 表达式

### 6.1 值类型

| 类型 | 字面量 | 说明 |
|---|---|---|
| Bool | `true` `false` | |
| Int | `[0-9]+` | 64 位整数 |
| Float | `[0-9]+.[0-9]+` | IEEE double |
| String | `"..."` | |

### 6.2 运算符与优先级（C 系，从高到低）

```
 ( )            一元 !  一元 -
 *  /  %        （% 仅 Int）
 +  -           （+ 对 String+String 为拼接）
 <  <=  >  >=   （仅数值，Int/Float 提升；不可链式）
 ==  !=         （同类比较；数值跨 Int/Float 可比；其余跨类型为错误）
 &&             （短路，要求 Bool）
 ||             （短路，要求 Bool）
```

### 6.3 EBNF

```ebnf
Expr    = OrExpr ;
OrExpr  = AndExpr { '||' AndExpr } ;
AndExpr = CmpExpr { '&&' CmpExpr } ;
CmpExpr = EqExpr [ ('<' | '<=' | '>' | '>=') EqExpr ] ;
EqExpr  = AddExpr [ ('==' | '!=') AddExpr ] ;
AddExpr = MulExpr { ('+' | '-') MulExpr } ;
MulExpr = Unary { ('*' | '/' | '%') Unary } ;
Unary   = ('!' | '-') Unary | Primary ;
Primary = '(' Expr ')' | Int | Float | String | 'true' | 'false' | Identifier ;
```

### 6.4 求值语义（运行时）

- 读取未定义变量 → `E_UNDEF_VAR`（不静默给默认值；强迫作者显式 `set` 初始化）。
- Int op Int = Int；任一方 Float → 双方提升 Float。Int `/` Int 截断向零；除零 → `E_DIV_ZERO`。
- `+=` 等复合赋值要求变量已定义且类型兼容；String 仅支持 `+`/`+=` 拼接。
- 逻辑运算严格要求 Bool；条件语境（jumpif、choice 选项 if）结果非 Bool → `E_TYPE`。
- 所有运行时错误：停机、广播 `RuntimeError` 事件、日志带源行号。

## 7. 作用域

- **局部作用域**：每章一个（VM 挂载的黑板），章节结束丢弃（进进度档快照）。
- **全局作用域**：跨章节存活（好感度、路线 flag），进全局档。
- 读：局部优先，未命中回落全局，仍未命中 → `E_UNDEF_VAR`。
- 写：`set` 写局部（变量已存在于全局时仍写**局部**，形成遮蔽）；`setg` 强制写全局。

## 8. 文本稳定 ID（本地化 / 已读跟踪）

自动生成：`<章节名>/<最近label>/<label内第几条text>`，如 `chapter01/lab_start/2`。
`end` 前 v0.1 不提供显式覆盖语法（预留：`text ... | ... id=xxx`，等本地化管道落地时启用）。
已读 ID 集合存全局档。

## 9. 错误码全表

**编译期（编辑期即暴露，带 1 起始行号）**

| 码 | 含义 |
|---|---|
| `E_TAB` | 任何位置出现 Tab |
| `E_INDENT` | 缩进空格数不是 4 的倍数 |
| `E_INDENT_LEVEL` | 缩进层级超过 choice 块允许的深度 |
| `E_UNKNOWN_CMD` | 未知的命令关键字（且未注册为自定义命令） |
| `E_ARG_COUNT` | 位置参数个数不符 |
| `E_ARG_BAD` | 参数内容非法（非法标识符、空 body、choice 块内非选项行等） |
| `E_BAD_PARAM` | 未知/重复的 `key=value` 参数 |
| `E_PARAM_VALUE` | 参数值越界（如 volume∉[0,1]、at 非 left/center/right） |
| `E_BAD_NUMBER` | 数字字面量格式错误 |
| `E_UNTERM_STRING` | 字符串未闭合 |
| `E_BAD_ESCAPE` | 未知转义序列 |
| `E_BAD_TOKEN` | 无法识别的词法记号（如单独的 `&`） |
| `E_BAD_EXPR` | 表达式语法错误（含比较链式） |
| `E_LABEL_NAME` | label 名不是合法标识符 |
| `E_DUP_LABEL` | label 重名 |
| `E_UNDEF_LABEL` | jump/jumpif/选项引用了不存在的 label |
| `E_EMPTY_CHOICE` | choice 无选项 |
| `E_NESTED_CHOICE` | choice 嵌套 |
| `E_MISSING_END` | 缺少 `end`（全文件至少一个） |
| `E_IO` | 注册表读文件失败 |

**运行时**

| 码 | 含义 |
|---|---|
| `E_UNDEF_VAR` | 读取未定义变量 |
| `E_TYPE` | 运算类型不匹配 |
| `E_DIV_ZERO` | 除零 / 模零 |

**警告（不阻断）**：`W_UNREACHABLE`（无条件跳转/终止后紧跟非 label 行）、`W_UNUSED_LABEL`（从未被引用的 label）。

## 10. 自定义命令与玩法断点（扩展点）

框架不假设演出词汇表；非 VN 玩法（调查、QTE、小游戏）经此处接入，**不改框架源码**。

### 10.1 编译期注册（严格模式，推荐）

注册 `FLeoCommandSpec`：命令名、位置参数个数上下限（`Max=-1` 不限）、命名参数白名单（空 = 不允许命名参数）。
违反 → 编辑期即报 `E_ARG_COUNT` / `E_BAD_PARAM` / `E_ARG_BAD`，带行号——扩展命令与内置命令拿到同等的前置校验。

### 10.2 宽松模式（兼容）

`SetCustomCommandNames` 只登记命令名，参数不做校验。

### 10.3 运行期处理器（三态返回）

- `Next`：瞬时完成，继续下一条命令；
- `Suspend`：处理器先调用 `VM.Suspend(<token>)` 把 VM 挂起为**外部断点**（token 须为标识符），再返回 `Suspend`；
- `Halt`：终止章节（处理器自行广播错误事件）。

恢复：`ULeoNarrativeSubsystem::ResumeWith(token, payload)` —— payload 写入**局部黑板键 `<token>`**，
脚本用 `jumpif` 读结果分流（与 choice→`last_choice` 同一模式；VM 指针不被外部驱动）。
断点期间 `skip`/`auto` 不生效（玩法必须真实完成）。

### 10.4 事件

自定义命令广播 `Custom` 事件（`CustomName` + `ExtraParams` 参数袋：`arg0..N` 为位置参数、命名参数按名存入），
玩法 UI / 表现层从事件流接手渲染。

自定义命令不得伪装控制流（不得直接改写 PC）——与铁律冲突的扩展会被拒绝合入。

### 10.5 示例（参见 Examples/LeoInvestigationDemo.cpp 与 Content/Scripts/chapter02.leo）

```leo
investigate scene_office mode=strict     # 挂起为 investigation 断点
jumpif investigation >= 2 -> lab_solved  # 调查完成后按发现数分流
```

### 10.6 框架预注册命令：`seq`（Level Sequencer 过场）

`seq` 由框架用 §10.1 同一机制预注册（Stage/LeoSequencerPerformer.cpp），**不是内核命令**——
证明扩展点足以承载演出类阻塞。清单（ULeoAssetManifest）把逻辑名映射到 `ULevelSequence` 资产。

```leo
seq lab_intro_cut wait=1 rate=1.0 start=0 loop=0
jumpif seq >= 1 -> cut_done   # seq 键 = 1 自然播完 / 0 缺资源或被跳过（永不软锁）
```

- `wait=1`（默认）：挂起为 `seq` 断点，播完 `OnFinished → ResumeWith("seq", 1)`；
  缺资源以 0 恢复走兜底分支；游戏侧 `SkipSequences()` 停播并以 0 恢复（跳过过场）；
- `wait=0`：即刻继续（氛围循环，跨章节存活，`loop=1` 配套）；
- `rate`/`start`：倍速与起始秒数；带 Camera Cut 轨道的序列自动接管镜头（引擎原生）。
- 存档语义：锚点落在 `seq` 命令上，读档重放过场（不重播的过场把 label 放在其后）。

示例：Content/Scripts/chapter03.leo；无头验证：`-run=LeoRun -exec="leo.demoseq"`（合成序列）。

## 11. 完整示例

```leo
# chapter01.leo —— LeoNarrative 演示剧本
bg bg_school_room
bgm bgm_daily01 volume=0.8
char 李雷 char_lico_smile at=left

label lab_start
set met_lico = 1
setg affection = 0
voice v0101_0001
text 李雷 | 早上好！今天也要一起回去吗？
text - | 放学后的走廊，夕阳把地板染成橙色。
choice
    当然可以 -> branch_yes
    今天有事 -> branch_no if affection >= 2
    沉默 -> branch_silent

label branch_yes
setg affection += 1
text 李雷 | 太好了，那我们在校门口见！
jump lab_ending

label branch_no
text 李雷 | 这样啊……那明天见。
jump lab_ending

label branch_silent
wait 800
text - | 你什么也没说，只是点了点头。
jumpif affection >= 1 && met_lico == 1 -> lab_ending

label lab_ending
bgm -
text - | —— 第一章 完 ——
end
```

## 12. 版本

- v0.1（2026-09）：首个实现版本。收窄项：无 `text` 显式 ID 覆盖、无条件表达式语法糖、无本地化管道。
- v0.11（2026-09-12，M6 通用化修订）：`end` 允许多个（多分支章节各自提前收束，删除 `E_AFTER_END`）；
  新增自定义命令严格 spec 校验与玩法断点（Suspend/ResumeWith，§10）。
- 规范改动流程：修改本文件 → 同步编译器 → 更新 golden 语料 → 跑 `LeoValidate` 回归。
