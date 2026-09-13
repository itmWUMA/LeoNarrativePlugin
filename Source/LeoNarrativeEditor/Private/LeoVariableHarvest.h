// 变量收割注册表（编辑器侧派生物，不落盘、不进运行时）：
// 脚本 set/setg 目标 + 图边副作用 Key = 写入；表达式引用（jumpif/choice/set 值/边条件/边副作用值）= 读取。
// 消费方：Details 拾取器/自动补全（Key 下拉、Condition/Expr 补全）。文本仍是唯一事实源，注册表随源推导。
#pragma once

#include "CoreMinimal.h"

struct FLeoKnownVar
{
	FName Name;
	bool bWritten = false; // 处处有写入 = "真变量"
	bool bGlobal = false;  // setg / bGlobal 写全局层
	bool bRead = false;    // 仅被读取（游戏代码定义的变量会落在这里）
	FString Kind;          // 字面量类型提示（"Int"/"String"/…，空 = 未知）
	FName Source;          // 首次见到的来源（脚本章节名 / 图资产名）
};

class FLeoVariableHarvest
{
public:
	static FLeoVariableHarvest& Get();

	void MarkStale() { bStale = true; }
	const TMap<FName, FLeoKnownVar>& GetVars();  // 过期时懒重建（仅游戏线程调用）
	TArray<FName> GetSortedNames() const;        // 拾取器候选，按名排序

private:
	void Rebuild();
	void HarvestScript(const FString& Path);
	void HarvestGraphAssets();

	TMap<FName, FLeoKnownVar> Vars;
	bool bStale = true;
};
