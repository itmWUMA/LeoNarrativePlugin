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

} // namespace leo
