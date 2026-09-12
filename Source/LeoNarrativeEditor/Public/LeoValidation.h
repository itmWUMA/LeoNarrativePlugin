// LeoNarrative 编辑器模块的共享校验入口：命令行 / 菜单 / 文件监听 / 校验面板 四处复用
#pragma once

#include "CoreMinimal.h"

DECLARE_MULTICAST_DELEGATE(FLeoOnScriptsRevalidated);

namespace LeoValidation
{
	// 单条诊断 / 核对结果
	struct FLeoCheckItem
	{
		FString File;        // 涉及文件（资产核对时为清单资产路径）
		int32 Line = 0;
		FString Code;        // 编译诊断码，或 NO_MANIFEST_ENTRY / ASSET_NOT_FOUND / ASSET_WRONG_TYPE
		FString Message;
		bool bError = false; // 计入失败数 / 面板红色显示
	};

	// 单文件结果
	struct FLeoFileResult
	{
		FString Path;
		bool bExpectClean = true; // false = golden fail 语料（期待有错误）
		bool bPass = true;        // 是否符合预期
		TArray<FLeoCheckItem> Items;
	};

	// 整轮校验摘要（校验面板的数据源）
	struct FLeoValidateSummary
	{
		TArray<FLeoFileResult> Files;
		TArray<FLeoCheckItem> AssetItems; // 资产引用核对（无清单时为空）
		FString ManifestPath;             // 参与核对的清单资产；空 = 未找到，核对跳过
		int32 Failures = 0;               // 预期不符的文件数
		int32 AssetErrors = 0;            // 资产核对错误条数
		bool AllGreen() const { return Failures == 0 && AssetErrors == 0; }
	};

	// 结构化校验（面板消费）。ManifestAssetPath 为空 = 自动发现工程内 ULeoAssetManifest
	FLeoValidateSummary ValidateAllStructured(const FString& ManifestAssetPath = FString());

	// 编译单个 .leo 文件并输出诊断日志（watcher 热校验用）。
	// bExpectClean=true 时期待零错误；false 时期待有错误（golden fail 语料）。
	bool ValidateFile(const FString& Path, bool bExpectClean);

	// 兼容旧入口：结构化校验 + 日志输出（菜单/命令行）。返回失败数（含资产核对错误）
	int32 ValidateAll(const FString& ManifestAssetPath = FString());

	// 脚本热校验完成通知（面板订阅后自动刷新）
	extern FLeoOnScriptsRevalidated OnScriptsRevalidated;
}
