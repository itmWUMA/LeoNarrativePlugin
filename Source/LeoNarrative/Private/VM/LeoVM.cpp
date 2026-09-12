#include "VM/LeoVM.h"
#include "ScriptRuntime/LeoScriptBridge.h"

DEFINE_LOG_CATEGORY_STATIC(LogLeoVM, Log, All);

// ---- 内置命令分发表（进程级，初始化一次）----
TMap<leo::ELeoCmd, ULeoVM::FHandler>& ULeoVM::HandlerTable()
{
	static TMap<leo::ELeoCmd, FHandler> Table = []
	{
		TMap<leo::ELeoCmd, FHandler> T;
		T.Add(leo::ELeoCmd::Nop, &ULeoVM::HandleNop);
		T.Add(leo::ELeoCmd::Label, &ULeoVM::HandleNop);      // label 编译后等价于 Nop
		T.Add(leo::ELeoCmd::Text, &ULeoVM::HandleText);
		T.Add(leo::ELeoCmd::Bg, &ULeoVM::HandleBg);
		T.Add(leo::ELeoCmd::Char, &ULeoVM::HandleChar);
		T.Add(leo::ELeoCmd::Bgm, &ULeoVM::HandleBgm);
		T.Add(leo::ELeoCmd::Se, &ULeoVM::HandleSe);
		T.Add(leo::ELeoCmd::Voice, &ULeoVM::HandleVoice);
		T.Add(leo::ELeoCmd::Wait, &ULeoVM::HandleWait);
		T.Add(leo::ELeoCmd::Jump, &ULeoVM::HandleJump);
		T.Add(leo::ELeoCmd::JumpIf, &ULeoVM::HandleJumpIf);
		T.Add(leo::ELeoCmd::Set, &ULeoVM::HandleSet);
		T.Add(leo::ELeoCmd::SetG, &ULeoVM::HandleSet);
		T.Add(leo::ELeoCmd::Choice, &ULeoVM::HandleChoice);
		T.Add(leo::ELeoCmd::End, &ULeoVM::HandleEnd);
		T.Add(leo::ELeoCmd::Custom, &ULeoVM::HandleCustom);
		return T;
	}();
	return Table;
}

TMap<FName, ULeoVM::FLeoCustomEntry>& ULeoVM::CustomCommands()
{
	static TMap<FName, FLeoCustomEntry> Table;
	return Table;
}

void ULeoVM::RegisterCustomCommand(FName Name, const LeoBridge::FLeoCmdSpec& Spec, FCustomHandler Handler)
{
	FLeoCustomEntry& E = CustomCommands().FindOrAdd(Name);
	E.bStrict = true;
	E.Spec = Spec;
	E.Handler = std::move(Handler);
	// 注册即镜像到纯内核——命令行校验（LeoValidate）等非游戏路径也能编译含自定义命令的剧本
	LeoBridge::SetCustomCommandSpecs(GetStrictCommandSpecs());
	LeoBridge::SetCustomCommandNames(GetLenientCommandNames());
}

void ULeoVM::RegisterCustomHandler(FName Name, FCustomHandler Handler)
{
	FLeoCustomEntry& E = CustomCommands().FindOrAdd(Name);
	E.bStrict = false;
	E.Handler = std::move(Handler);
	LeoBridge::SetCustomCommandNames(GetLenientCommandNames());
}

TArray<FString> ULeoVM::GetLenientCommandNames()
{
	TArray<FString> Names;
	for (const TPair<FName, FLeoCustomEntry>& KV : CustomCommands())
	{
		if (!KV.Value.bStrict) { Names.Add(KV.Key.ToString()); }
	}
	return Names;
}

TArray<LeoBridge::FLeoCmdSpec> ULeoVM::GetStrictCommandSpecs()
{
	TArray<LeoBridge::FLeoCmdSpec> Specs;
	for (const TPair<FName, FLeoCustomEntry>& KV : CustomCommands())
	{
		if (KV.Value.bStrict) { Specs.Add(KV.Value.Spec); }
	}
	return Specs;
}

void ULeoVM::Init(FName InChapter, const TSharedPtr<leo::FLeoProgram>& InProgram,
                  UNarrativeBlackboard* InLocal, UNarrativeBlackboard* InGlobal)
{
	Chapter = InChapter;
	Program = InProgram;
	LocalBB = InLocal;
	GlobalBB = InGlobal;
	PC = 0;
	State = ELeoVMState::Running;

	FLeoEvent Ev;
	Ev.Kind = ELeoEventKind::ChapterStart;
	Ev.Chapter = Chapter;
	Broadcast(std::move(Ev));
}

void ULeoVM::Tick(float DeltaSeconds)
{
	if (State == ELeoVMState::WaitTimer)
	{
		WaitRemaining -= DeltaSeconds;
		if (WaitRemaining <= 0.f)
		{
			if (bNeedSkipCurrent) { ++PC; bNeedSkipCurrent = false; }
			SuspendToken = NAME_None;
			State = ELeoVMState::Running;
		}
	}
	if (!Program.IsValid())
	{
		State = ELeoVMState::Finished;
		return;
	}
	while (State == ELeoVMState::Running)
	{
		if (PC < 0 || PC >= static_cast<int32>(Program->Commands.size()))
		{
			RuntimeError(leo::ELeoDiag::E_BAD_EXPR, PC + 1, "执行越界（缺少 end？）");
			return;
		}
		const leo::FLeoCommand& Cmd = Program->Commands[PC];
		FHandler H = HandlerTable().FindRef(Cmd.Kind);
		if (!H)
		{
			RuntimeError(leo::ELeoDiag::E_UNKNOWN_CMD, Cmd.Line, "命令没有注册处理器: " + std::to_string(static_cast<int>(Cmd.Kind)));
			return;
		}
		const EResult R = (this->*H)(Cmd);
		switch (R)
		{
		case EResult::Next: ++PC; break;
		case EResult::Jumped: break;            // 处理器已设置 PC
		case EResult::Block:                    // PC 留在阻塞命令上，恢复时跳过
			bNeedSkipCurrent = true;
			return;
		case EResult::Halt: return;             // Finished / RuntimeError
		}
	}
}

bool ULeoVM::Advance()
{
	if (State != ELeoVMState::WaitClick) { return false; }
	if (bNeedSkipCurrent) { ++PC; bNeedSkipCurrent = false; }
	SuspendToken = NAME_None;
	State = ELeoVMState::Running;
	return true;
}

// ---- 外部断点 ----

bool ULeoVM::Suspend(FName Token)
{
	// 只允许在命令执行期间（Running）挂起——即处理器内部调用
	if (State != ELeoVMState::Running) { return false; }
	SuspendToken = Token;
	State = ELeoVMState::WaitExternal;
	return true;
}

bool ULeoVM::ResumeWith(FName Token, const leo::FLeoValue& Payload)
{
	if (State != ELeoVMState::WaitExternal || SuspendToken != Token) { return false; }
	// 结果写黑板，由脚本读黑板分流（铁律：外部不驱动 VM 指针）
	if (LocalBB)
	{
		LocalBB->SetValue(Token, Payload);
	}
	if (bNeedSkipCurrent) { ++PC; bNeedSkipCurrent = false; }
	SuspendToken = NAME_None;
	State = ELeoVMState::Running;
	return true;
}

void ULeoVM::EmitCustomEvent(const leo::FLeoCommand& C)
{
	FLeoEvent Ev;
	Ev.Kind = ELeoEventKind::Custom;
	Ev.CustomName = FName(C.CustomName.c_str());
	Ev.Chapter = Chapter;
	Ev.Line = C.Line;
	for (const leo::FLeoParam& P : C.Params)
	{
		Ev.ExtraParams.Add(FName(P.Key.c_str()), LeoBridge::ToFString(P.Value));
	}
	for (size_t i = 0; i < C.CustomArgs.size(); ++i)
	{
		Ev.ExtraParams.Add(FName(*FString::Printf(TEXT("arg%d"), static_cast<int32>(i))),
			LeoBridge::ToFString(C.CustomArgs[i]));
	}
	OnEvent.Broadcast(Ev);
}

bool ULeoVM::Choose(int32 Index)
{
	if (State != ELeoVMState::WaitChoice || !Program.IsValid()) { return false; }
	if (Index < 0 || Index >= ActiveOptions.Num())
	{
		UE_LOG(LogLeoVM, Warning, TEXT("Choose(%d) 越界（共 %d 项）"), Index, ActiveOptions.Num());
		return false;
	}
	const leo::FLeoOption& Opt = ActiveOptions[Index];
	// 架构铁律：选择结果写黑板，由脚本 jumpif 分流，VM 不直接被外部驱动指针
	if (LocalBB)
	{
		LocalBB->SetValue(TEXT("last_choice"), leo::FLeoValue::MakeInt(Index));
	}
	FLeoEvent Ev;
	Ev.Kind = ELeoEventKind::ChoiceMade;
	Ev.Chapter = Chapter;
	Ev.Line = Program->Commands[PC].Line;
	Ev.ChoiceIndex = Index;
	Ev.TextId = LeoBridge::ToFString(Opt.TargetLabel);
	Broadcast(std::move(Ev));

	PC = Opt.TargetIndex;
	ActiveOptions.Reset();
	bNeedSkipCurrent = false; // PC 已被跳转设置，无需跳过
	SuspendToken = NAME_None;
	State = ELeoVMState::Running;
	return true;
}

bool ULeoVM::GetAnchor(FName& OutLabel, int32& OutOffset) const
{
	if (!Program.IsValid() || PC < 0 || PC >= static_cast<int32>(Program->Commands.size())) { return false; }
	for (int32 i = PC; i >= 0; --i)
	{
		if (Program->Commands[i].Kind == leo::ELeoCmd::Label)
		{
			OutLabel = FName(Program->Commands[i].Label.c_str());
			OutOffset = PC - i;
			return true;
		}
	}
	return false;
}

bool ULeoVM::RestoreAnchor(FName Label, int32 Offset)
{
	if (!Program.IsValid()) { return false; }
	const auto It = Program->LabelIndex.find(std::string(TCHAR_TO_UTF8(*Label.ToString())));
	if (It == Program->LabelIndex.end()) { return false; }
	const int32 Target = It->second + Offset;
	if (Target < 0 || Target >= static_cast<int32>(Program->Commands.size())) { return false; }
	PC = Target;
	bNeedSkipCurrent = false; // 锚点重放：从 Target 命令开始重新执行
	ActiveOptions.Reset();
	SuspendToken = NAME_None;
	State = ELeoVMState::Running;
	return true;
}

void ULeoVM::ComputeTextAnchor(int32 TextPC, FName& OutLabel, int32& OutSeq) const
{
	OutSeq = 0;
	for (int32 i = TextPC; i >= 0; --i)
	{
		const leo::FLeoCommand& C = Program->Commands[i];
		if (C.Kind == leo::ELeoCmd::Text && i < TextPC) { ++OutSeq; }
		if (C.Kind == leo::ELeoCmd::Label)
		{
			OutLabel = FName(C.Label.c_str());
			return;
		}
	}
	OutLabel = TEXT("_root");
}

// ---- 命令处理器 ----

ULeoVM::EResult ULeoVM::HandleNop(const leo::FLeoCommand&) { return EResult::Next; }

ULeoVM::EResult ULeoVM::HandleText(const leo::FLeoCommand& C)
{
	FName Label;
	int32 Seq = 0;
	ComputeTextAnchor(PC, Label, Seq);

	FLeoEvent Ev;
	Ev.Kind = ELeoEventKind::Text;
	Ev.Chapter = Chapter;
	Ev.Line = C.Line;
	Ev.Speaker = LeoBridge::ToFString(C.Speaker);
	Ev.Text = LeoBridge::ToFString(C.Body);
	Ev.TextId = FString::Printf(TEXT("%s/%s/%d"), *Chapter.ToString(), *Label.ToString(), Seq);
	Broadcast(std::move(Ev));
	SuspendToken = TEXT("click");
	State = ELeoVMState::WaitClick;
	return EResult::Block;
}

ULeoVM::EResult ULeoVM::HandleBg(const leo::FLeoCommand& C)
{
	FLeoEvent Ev;
	Ev.Kind = ELeoEventKind::Bg;
	Ev.Chapter = Chapter;
	Ev.Line = C.Line;
	Ev.AssetId = FName(C.AssetId.c_str());
	Ev.Transition = TEXT("cut");
	Ev.Duration = 0.5f;
	for (const leo::FLeoParam& P : C.Params)
	{
		if (P.Key == "transition") { Ev.Transition = LeoBridge::ToFString(P.Value); }
		else if (P.Key == "duration") { Ev.Duration = FCString::Atof(UTF8_TO_TCHAR(P.Value.c_str())); }
	}
	Broadcast(std::move(Ev));
	return EResult::Next;
}

ULeoVM::EResult ULeoVM::HandleChar(const leo::FLeoCommand& C)
{
	FLeoEvent Ev;
	Ev.Kind = ELeoEventKind::Char;
	Ev.Chapter = Chapter;
	Ev.Line = C.Line;
	Ev.Slot = LeoBridge::ToFString(C.Slot);
	Ev.AssetId = FName(C.AssetId.c_str());
	for (const leo::FLeoParam& P : C.Params)
	{
		if (P.Key == "at") { Ev.At = LeoBridge::ToFString(P.Value); }
		else if (P.Key == "pose") { Ev.Pose = LeoBridge::ToFString(P.Value); }
		else if (P.Key == "motion") { Ev.Motion = LeoBridge::ToFString(P.Value); }
	}
	Broadcast(std::move(Ev));
	return EResult::Next;
}

ULeoVM::EResult ULeoVM::HandleBgm(const leo::FLeoCommand& C)
{
	FLeoEvent Ev;
	Ev.Kind = ELeoEventKind::Bgm;
	Ev.Chapter = Chapter;
	Ev.Line = C.Line;
	Ev.AssetId = FName(C.AssetId.c_str());
	Ev.Volume = 1.f;
	for (const leo::FLeoParam& P : C.Params)
	{
		if (P.Key == "volume") { Ev.Volume = FCString::Atof(UTF8_TO_TCHAR(P.Value.c_str())); }
		else if (P.Key == "fade") { Ev.Fade = FCString::Atof(UTF8_TO_TCHAR(P.Value.c_str())); }
	}
	Broadcast(std::move(Ev));
	return EResult::Next;
}

ULeoVM::EResult ULeoVM::HandleSe(const leo::FLeoCommand& C)
{
	FLeoEvent Ev;
	Ev.Kind = ELeoEventKind::Se;
	Ev.Chapter = Chapter;
	Ev.Line = C.Line;
	Ev.AssetId = FName(C.AssetId.c_str());
	Ev.Volume = 1.f;
	for (const leo::FLeoParam& P : C.Params)
	{
		if (P.Key == "volume") { Ev.Volume = FCString::Atof(UTF8_TO_TCHAR(P.Value.c_str())); }
	}
	Broadcast(std::move(Ev));
	return EResult::Next;
}

ULeoVM::EResult ULeoVM::HandleVoice(const leo::FLeoCommand& C)
{
	FLeoEvent Ev;
	Ev.Kind = ELeoEventKind::Voice;
	Ev.Chapter = Chapter;
	Ev.Line = C.Line;
	Ev.AssetId = FName(C.AssetId.c_str());
	Broadcast(std::move(Ev));
	return EResult::Next;
}

ULeoVM::EResult ULeoVM::HandleWait(const leo::FLeoCommand& C)
{
	WaitRemaining = static_cast<float>(C.Millis) / 1000.f;
	SuspendToken = TEXT("timer");
	State = ELeoVMState::WaitTimer;
	return EResult::Block;
}

ULeoVM::EResult ULeoVM::HandleJump(const leo::FLeoCommand& C)
{
	PC = C.TargetIndex;
	return EResult::Jumped;
}

ULeoVM::EResult ULeoVM::HandleJumpIf(const leo::FLeoCommand& C)
{
	leo::FLeoValue V;
	leo::ELeoDiag Code;
	std::string Msg;
	if (!EvalExpr(C.ExprIndex, V, Code, Msg))
	{
		RuntimeError(Code, C.Line, Msg);
		return EResult::Halt;
	}
	if (V.Kind != leo::FLeoValue::EKind::Bool)
	{
		RuntimeError(leo::ELeoDiag::E_TYPE, C.Line, "jumpif 条件必须是 Bool");
		return EResult::Halt;
	}
	if (V.B) { PC = C.TargetIndex; return EResult::Jumped; }
	return EResult::Next;
}

ULeoVM::EResult ULeoVM::HandleSet(const leo::FLeoCommand& C)
{
	// set 写局部（遮蔽全局），setg 强制写全局——spec §7
	UNarrativeBlackboard* Scope = (C.Kind == leo::ELeoCmd::SetG) ? GlobalBB.Get() : LocalBB.Get();
	if (!Scope)
	{
		RuntimeError(leo::ELeoDiag::E_TYPE, C.Line, "黑板未初始化");
		return EResult::Halt;
	}
	if (!ApplySetOp(Scope, C))
	{
		return EResult::Halt; // ApplySetOp 已广播 RuntimeError
	}
	return EResult::Next;
}

ULeoVM::EResult ULeoVM::HandleChoice(const leo::FLeoCommand& C)
{
	ActiveOptions.Reset();
	for (const leo::FLeoOption& Opt : C.Options)
	{
		if (Opt.ExprIndex >= 0)
		{
			leo::FLeoValue V;
			leo::ELeoDiag Code;
			std::string Msg;
			if (!EvalExpr(Opt.ExprIndex, V, Code, Msg))
			{
				RuntimeError(Code, C.Line, Msg);
				return EResult::Halt;
			}
			if (V.Kind != leo::FLeoValue::EKind::Bool)
			{
				RuntimeError(leo::ELeoDiag::E_TYPE, C.Line, "选项条件必须是 Bool");
				return EResult::Halt;
			}
			if (!V.B) { continue; } // 条件为假：不展示
		}
		ActiveOptions.Add(Opt);
	}
	if (ActiveOptions.Num() == 0)
	{
		RuntimeError(leo::ELeoDiag::E_TYPE, C.Line, "choice 的所有选项都被条件过滤掉了");
		return EResult::Halt;
	}
	FLeoEvent Ev;
	Ev.Kind = ELeoEventKind::ChoiceShown;
	Ev.Chapter = Chapter;
	Ev.Line = C.Line;
	for (const leo::FLeoOption& Opt : ActiveOptions)
	{
		FLeoEventOption EO;
		EO.Text = LeoBridge::ToFString(Opt.Text);
		EO.TargetLabel = LeoBridge::ToFString(Opt.TargetLabel);
		Ev.Options.Add(std::move(EO));
	}
	Broadcast(std::move(Ev));
	SuspendToken = TEXT("choice");
	State = ELeoVMState::WaitChoice;
	return EResult::Block;
}

ULeoVM::EResult ULeoVM::HandleEnd(const leo::FLeoCommand& C)
{
	FLeoEvent Ev;
	Ev.Kind = ELeoEventKind::ChapterEnd;
	Ev.Chapter = Chapter;
	Ev.Line = C.Line;
	Broadcast(std::move(Ev));
	State = ELeoVMState::Finished;
	return EResult::Halt;
}

ULeoVM::EResult ULeoVM::HandleCustom(const leo::FLeoCommand& C)
{
	FLeoCustomEntry* E = CustomCommands().Find(FName(C.CustomName.c_str()));
	if (!E || !E->Handler)
	{
		RuntimeError(leo::ELeoDiag::E_UNKNOWN_CMD, C.Line, "自定义命令没有注册运行时处理器: " + C.CustomName);
		return EResult::Halt;
	}
	const ELeoCustomResult R = E->Handler(*this, C);
	switch (R)
	{
	case ELeoCustomResult::Next:
		return EResult::Next;
	case ELeoCustomResult::Suspend:
		// 处理器应已调用 Suspend() 挂起
		if (State != ELeoVMState::WaitExternal)
		{
			RuntimeError(leo::ELeoDiag::E_TYPE, C.Line,
				"自定义命令返回 Suspend 但未调用 Suspend(): " + C.CustomName);
			return EResult::Halt;
		}
		return EResult::Block;
	case ELeoCustomResult::Halt:
		State = ELeoVMState::Finished;
		return EResult::Halt;
	}
	return EResult::Next;
}

// ---- 求值与赋值 ----

bool ULeoVM::EvalExpr(int32 ExprIndex, leo::FLeoValue& Out, leo::ELeoDiag& OutCode, std::string& OutMsg)
{
	if (!Program.IsValid() || ExprIndex < 0 || ExprIndex >= static_cast<int32>(Program->ExprPool.size()))
	{
		OutCode = leo::ELeoDiag::E_BAD_EXPR;
		OutMsg = "表达式索引无效";
		return false;
	}
	// 变量解析：局部优先回落全局（黑板链）
	const leo::FLeoVarResolver Resolver = [this](const std::string& Name, leo::FLeoValue& OutV) -> bool
	{
		if (LocalBB && LocalBB->GetValue(FName(Name.c_str()), OutV)) { return true; }
		if (GlobalBB && GlobalBB->GetValue(FName(Name.c_str()), OutV)) { return true; }
		return false;
	};
	if (!leo::LeoEval(*Program->ExprPool[ExprIndex], Resolver, Out, OutCode, OutMsg))
	{
		return false;
	}
	return true;
}

bool ULeoVM::ApplySetOp(UNarrativeBlackboard* Scope, const leo::FLeoCommand& C)
{
	const FName Key(C.Name.c_str());
	leo::FLeoValue R;
	leo::ELeoDiag Code;
	std::string Msg;
	if (!EvalExpr(C.ExprIndex, R, Code, Msg))
	{
		RuntimeError(Code, C.Line, Msg);
		return false;
	}
	if (C.Op == "=")
	{
		Scope->SetValue(Key, R);
		return true;
	}
	// 复合赋值：读取已有值（沿作用域链）
	leo::FLeoValue Old;
	if (!Scope->GetValue(Key, Old))
	{
		RuntimeError(leo::ELeoDiag::E_UNDEF_VAR, C.Line, "复合赋值要求变量已定义: " + C.Name);
		return false;
	}
	leo::FLeoValue NewV;
	const char Op = C.Op[0]; // '+' '-' '*' '/'
	if (Op == '+' && Old.Kind == leo::FLeoValue::EKind::String && R.Kind == leo::FLeoValue::EKind::String)
	{
		NewV = leo::FLeoValue::MakeString(Old.S + R.S);
	}
	else if (Old.IsNumber() && R.IsNumber())
	{
		if (Old.Kind == leo::FLeoValue::EKind::Int && R.Kind == leo::FLeoValue::EKind::Int)
		{
			const int64_t A = Old.I, B = R.I;
			if (Op == '+') { NewV = leo::FLeoValue::MakeInt(A + B); }
			else if (Op == '-') { NewV = leo::FLeoValue::MakeInt(A - B); }
			else if (Op == '*') { NewV = leo::FLeoValue::MakeInt(A * B); }
			else // '/'
			{
				if (B == 0) { RuntimeError(leo::ELeoDiag::E_DIV_ZERO, C.Line, "Int 除零"); return false; }
				NewV = leo::FLeoValue::MakeInt(A / B);
			}
		}
		else
		{
			const double A = Old.AsDouble(), B = R.AsDouble();
			if (Op == '+') { NewV = leo::FLeoValue::MakeFloat(A + B); }
			else if (Op == '-') { NewV = leo::FLeoValue::MakeFloat(A - B); }
			else if (Op == '*') { NewV = leo::FLeoValue::MakeFloat(A * B); }
			else
			{
				if (B == 0.0) { RuntimeError(leo::ELeoDiag::E_DIV_ZERO, C.Line, "Float 除零"); return false; }
				NewV = leo::FLeoValue::MakeFloat(A / B);
			}
		}
	}
	else
	{
		RuntimeError(leo::ELeoDiag::E_TYPE, C.Line,
			"复合赋值类型不兼容: " + C.Name + " " + C.Op + "（" + Old.ToString() + " 与 " + R.ToString() + "）");
		return false;
	}
	Scope->SetValue(Key, NewV);
	return true;
}

void ULeoVM::Broadcast(FLeoEvent&& Ev)
{
	OnEvent.Broadcast(Ev);
}

void ULeoVM::RuntimeError(leo::ELeoDiag Code, int32 Line, const std::string& Msg)
{
	State = ELeoVMState::Finished;
	FLeoEvent Ev;
	Ev.Kind = ELeoEventKind::RuntimeError;
	Ev.Chapter = Chapter;
	Ev.Line = Line;
	Ev.DiagCode = LeoBridge::DiagName(Code);
	Ev.DiagMsg = LeoBridge::ToFString(Msg);
	UE_LOG(LogLeoVM, Error, TEXT("[VM] 运行时错误 %s(%d): %s"), *Chapter.ToString(), Line, *Ev.DiagMsg);
	OnEvent.Broadcast(Ev);
}
