// 逻辑名清单资产：脚本里的 bgm_daily01 等逻辑名 → UE 资产软路径
// 脚本永不出现资产路径（铁律 #2）；清单资产在编辑器里创建并交给子系统
//
// 类别节形态：类别集合可扩展——内置 bgm/se/voice/bg/char/seq 只是预填的默认节，
// 项目可为自定义命令（如 video）增补自己的类别节；每节可声明期望资产类
// （校验按它核对类型；空 = 只核存在性）。内置类别的期望类由框架默认表兜底，
// 节声明优先于默认表。旧平表 Assets 仅存量回落。
#pragma once

#include "Engine/DataAsset.h"
#include "LeoAssetManifest.generated.h"

// 一个类别节：同类条目 + 该类别的期望资产类声明
USTRUCT()
struct LEONARRATIVE_API FLeoManifestCategory
{
	GENERATED_BODY()

	// 类别名 = 命令侧的类别 token（内置 bgm/se/voice/bg/char/seq；自定义命令可扩展）
	UPROPERTY(EditAnywhere, Category = "Leo")
	FName Name;

	// 该类别的期望资产类（校验核对类型用；空 = 内置类别走框架默认，其余只核存在性）
	UPROPERTY(EditAnywhere, Category = "Leo")
	TSoftClassPtr<UObject> ExpectedClass;

	UPROPERTY(EditAnywhere, Category = "Leo")
	TMap<FName, FSoftObjectPath> Assets;
};

UCLASS()
class LEONARRATIVE_API ULeoAssetManifest : public UDataAsset
{
	GENERATED_BODY()

public:
	// 类别节列表（专属清单编辑器在此增删类别；重复类别名以首节为准，校验会报错）
	UPROPERTY(EditAnywhere, Category = "Leo")
	TArray<FLeoManifestCategory> Categories;

	// 旧平表（存量数据，无类别信息）：类别节未命中时回落；建议迁入类别节
	UPROPERTY(EditAnywhere, Category = "Leo|兼容旧数据")
	TMap<FName, FSoftObjectPath> Assets;

	// ---- 旧分表字段（M9 形态，仅作 PostLoad 迁移源，新代码勿写）----
	UPROPERTY()
	TMap<FName, FSoftObjectPath> Bgm;
	UPROPERTY()
	TMap<FName, FSoftObjectPath> Se;
	UPROPERTY()
	TMap<FName, FSoftObjectPath> Voice;
	UPROPERTY()
	TMap<FName, FSoftObjectPath> Bg;
	UPROPERTY()
	TMap<FName, FSoftObjectPath> Char;
	UPROPERTY()
	TMap<FName, FSoftObjectPath> Seq;

	virtual void PostLoad() override;

	// 取类别节（同名重复时首个）
	const FLeoManifestCategory* FindCategory(FName Category) const;
	FLeoManifestCategory* FindCategory(FName Category);
	// 逻辑名所在的类别节名（跨节查找；不在任何节返回 false）
	bool FindCategoryOf(FName LogicalId, FName& OutCategory) const;

	// 类别感知解析（章节预载用）：先查该类别节，未命中回落旧平表；
	// 不做跨节查找（跨节错位由校验报错，预载宁缺毋滥，使用点同步加载兜底）
	bool TryResolve(FName Category, FName LogicalId, FSoftObjectPath& OutPath) const;
	// 无类别解析（表现层使用点）：类别节按数组序查全部 → 旧平表
	bool TryResolve(FName LogicalId, FSoftObjectPath& OutPath) const;

	// 内置类别名（命令语义绑定；与内核收集器 token 同名）
	static FName BgmCategory()   { return TEXT("bgm"); }
	static FName SeCategory()    { return TEXT("se"); }
	static FName VoiceCategory() { return TEXT("voice"); }
	static FName BgCategory()    { return TEXT("bg"); }
	static FName CharCategory()  { return TEXT("char"); }
	static FName SeqCategory()   { return TEXT("seq"); }

	// 内置类别的框架默认期望类（bgm/se/voice→USoundBase、seq→ULevelSequence、
	// bg/char→nullptr 只核存在性）。类别节上的声明优先于默认表
	static const UClass* GetBuiltInExpectedClass(FName Category);
	// 该节类型核对实际使用的期望类：节声明 → 框架默认 → nullptr
	const UClass* ResolveExpectedClass(const FLeoManifestCategory& Cat) const;
};
