// .leo 编译产物：命令列表 + label 跳转表（纯 C++ 数据模型）
// 对应规范：docs/leo-spec.md §5 命令全表
#pragma once

#include "Script/LeoExpr.h"
#include "Script/LeoTypes.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace leo
{

enum class ELeoCmd : uint8_t
{
	Nop,      // 占位（label 编译后即 Nop）
	Label,    // 跳转锚点
	Text,     // 对话（阻塞）
	Bg, Char, Bgm, Se, Voice, // 演出指令（瞬时）
	Wait,     // 计时阻塞
	Jump, JumpIf,
	Set, SetG, // 局部/全局黑板写
	Choice,   // 选项（阻塞）
	End,
	Custom,   // 注册扩展的自定义命令
};

// key=value 命名参数（值保留原文，由消费方按需转换）
struct FLeoParam
{
	std::string Key;
	std::string Value;
};

// choice 选项
struct FLeoOption
{
	std::string Text;        // 显示文本（原文）
	std::string TextId;      // 显式稳定 ID（空 = 由框架按 章节/label/c序号/选项号 生成）
	std::string TargetLabel; // 目标 label 名
	int TargetIndex = -1;    // 编译后回填的命令索引
	int ExprIndex = -1;      // 条件表达式池索引；-1 = 无条件
};

// 命令 = 数据（Command 模式的数据形态，不建继承树）。字段按命令取用，语义见命名。
struct FLeoCommand
{
	ELeoCmd Kind = ELeoCmd::Nop;
	int Line = 0;                 // 源行号（1 起始），运行期诊断/调试用

	// text
	std::string Speaker;          // 空串 = 旁白（脚本里的 "-" 已规范化）
	std::string Body;
	std::string TextId;           // 显式稳定 ID（空 = 自动生成 章节/label/序号；本地化送翻前 freeze 写入）
	// bg/char/bgm/se/voice 的逻辑资源名；char/bgm 用 "-" 表示移除/停止
	std::string AssetId;
	// char 槽位名（宽松 Word，允许 CJK）
	std::string Slot;
	// label/定义名；jump/jumpif 目标
	std::string Label;
	int TargetIndex = -1;         // 编译后回填的跳转目标命令索引
	// set/setg
	std::string Name;             // 变量名
	std::string Op;               // "=", "+=", "-=", "*=", "/="
	int ExprIndex = -1;           // set/jumpif 的表达式池索引
	// wait
	int Millis = 0;
	// 通用命名参数
	std::vector<FLeoParam> Params;
	// choice
	std::vector<FLeoOption> Options;
	// 自定义命令
	std::string CustomName;
	std::vector<std::string> CustomArgs; // 位置参数原文（token 化后的值）
};

// 一章的编译产物。源文本是唯一事实源，本结构永不持久化（Transient 派生物）。
struct FLeoProgram
{
	std::string SourceName;                                   // 章节 ID（文件名去扩展）
	std::vector<FLeoCommand> Commands;                        // 展平后的命令序列
	std::vector<FLeoExprPtr> ExprPool;                        // 表达式池，命令按索引引用
	std::unordered_map<std::string, int> LabelIndex;          // label 名 → 命令索引
	bool Ok = false;                                          // 无编译期错误
	std::vector<FLeoDiag> Diags;                              // 全部诊断（含警告）
};

// 变量使用统计（编辑器收割注册表 / 拼写检查用，内核自身不消费）
struct FLeoVarUsage
{
	bool bRead = false;       // 被表达式引用过（set 值 / jumpif / choice 条件）
	bool bWritten = false;    // set/setg 目标
	bool bGlobal = false;     // setg 写全局层
	FLeoValue::EKind LitKind = FLeoValue::EKind::Null; // 值为根字面量时的类型提示（非字面量 = Null）
};

// 整章变量使用收集：名字 → 使用信息（合并进 Out，跨章收割时反复调用）
void LeoCollectVarUsage(const FLeoProgram& P, std::unordered_map<std::string, FLeoVarUsage>& Out);

} // namespace leo
