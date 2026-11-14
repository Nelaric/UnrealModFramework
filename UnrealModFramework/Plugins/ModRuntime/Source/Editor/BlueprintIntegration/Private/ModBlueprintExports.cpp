// Copyright (c) 2026 Nelaric Contributors
#include "ModBlueprintExports.h"

#include "BlueprintActionDatabase.h"

#include "ModBlueprintExportGraphSchema.h"
#include "ModExportMetadataOptions.h"
#include "ModExportMetadataSchema.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphUtilities.h"
#include "Engine/Blueprint.h"
#include "FindInBlueprintManager.h"
#include "Framework/Notifications/NotificationManager.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Variable.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/Kismet2NameValidators.h"
#include "KismetCompiler.h"
#include "Misc/UObjectToken.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "ModBlueprintExports"

namespace
{
const TCHAR* GetExportPrefix(const UClass* EntryClass)
{
	if (EntryClass->IsChildOf<UModBlueprintAPIExport>())
	{
		return TEXT("ModAPI");
	}
	if (EntryClass->IsChildOf<UModBlueprintEventExport>())
	{
		return TEXT("ModEvent");
	}
	return TEXT("ModHook");
}

FString MakeExportName(const UClass* EntryClass, const FString& DisplayName)
{
	return FString::Printf(TEXT("%s_%s"), GetExportPrefix(EntryClass), *DisplayName);
}

FString NormalizeDisplayName(const UClass* EntryClass, const FString& NewName)
{
	FString Name = NewName.TrimStartAndEnd();
	const FString Prefix = FString::Printf(TEXT("%s_"), GetExportPrefix(EntryClass));
	if (Name.StartsWith(Prefix))
	{
		Name.RightChopInline(Prefix.Len());
	}
	return Name.TrimStartAndEnd();
}

void ReportExportError(FCompilerResultsLog& Log, const UModBlueprintExportEntry* Entry, const FText& Error)
{
	const TSharedRef<FTokenizedMessage> Message = Log.Error(TEXT("Mod export "));
	Message->AddToken(FUObjectToken::Create(Entry, Entry->GetExportDisplayName()));
	Message->AddToken(FTextToken::Create(FText::Format(LOCTEXT("ExportError", ": {0}"), Error)));
}

bool ValidateHookPolicy(FCompilerResultsLog& Log, const UModBlueprintHookExport* Entry)
{
	namespace Schema = Mod::ExportMetadata::Schema;
	if (Entry->AllowedStages.IsEmpty())
	{
		ReportExportError(Log, Entry, LOCTEXT("MissingHookStage", "Hook 至少需要开放一个阶段。"));
		return false;
	}
	const UEnum* StageEnum = StaticEnum<EModBlueprintHookStage>();
	TSet<FString> Stages;
	for (const EModBlueprintHookStage Stage : Entry->AllowedStages)
	{
		const FString Name = StageEnum->GetNameStringByValue(static_cast<int64>(Stage));
		if (!Schema::IsHookStage(TCHAR_TO_UTF8(*Name)) || Stages.Contains(Name))
		{
			ReportExportError(Log, Entry, LOCTEXT("InvalidHookStage", "开放阶段包含无效值或重复值。"));
			return false;
		}
		Stages.Add(Name);
	}
	const UEnum* OperationEnum = StaticEnum<EModBlueprintHookOperation>();
	TSet<EModBlueprintHookOperation> Operations;
	for (const EModBlueprintHookOperation Operation : Entry->AllowedOperations)
	{
		const FString Name = OperationEnum->GetNameStringByValue(static_cast<int64>(Operation));
		const Schema::FHookOperationSpec* Spec = Schema::FindHookOperation(TCHAR_TO_UTF8(*Name));
		if (!Spec || Operations.Contains(Operation))
		{
			ReportExportError(Log, Entry, LOCTEXT("InvalidHookOperation", "允许操作包含无效值或重复值。"));
			return false;
		}
		Operations.Add(Operation);
		const FString RequiredStage = UTF8_TO_TCHAR(Spec->Stage);
		if (!Stages.Contains(RequiredStage))
		{
			ReportExportError(Log, Entry,
			                  FText::Format(LOCTEXT("HookOperationNeedsStage", "Hook 操作 {0} 需要开放 {1} 阶段。"),
			                                FText::FromString(Name), FText::FromString(RequiredStage)));
			return false;
		}
	}
	return true;
}

bool HasExportName(const UModBlueprintExports* Exports, const UModBlueprintExportEntry* IgnoredEntry, const FName Name)
{
	for (const UModBlueprintAPIExport* Entry : Exports->APIs)
	{
		if (Entry && Entry != IgnoredEntry && Entry->GetExportName() == Name)
		{
			return true;
		}
	}
	for (const UModBlueprintEventExport* Entry : Exports->Events)
	{
		if (Entry && Entry != IgnoredEntry && Entry->GetExportName() == Name)
		{
			return true;
		}
	}
	for (const UModBlueprintHookExport* Entry : Exports->Hooks)
	{
		if (Entry && Entry != IgnoredEntry && Entry->GetExportName() == Name)
		{
			return true;
		}
	}
	return false;
}

bool ValidateDisplayName(const UBlueprint* Blueprint, const UClass* EntryClass, const UModBlueprintExportEntry* Entry,
                         const FString& NewName, FText& ErrorMessage)
{
	const FString Name = NormalizeDisplayName(EntryClass, NewName);
	const FString ExportName = MakeExportName(EntryClass, Name);
	if (!Blueprint || Name.IsEmpty())
	{
		ErrorMessage = LOCTEXT("EmptyName", "名称不能为空");
		return false;
	}
	if (ExportName.Len() > FKismetNameValidator::GetMaximumNameLength())
	{
		ErrorMessage = LOCTEXT("NameTooLong", "加上 Mod 前缀后的名称不能超过 100 个字符");
		return false;
	}
	if (!FName::IsValidXName(ExportName, INVALID_OBJECTNAME_CHARACTERS, &ErrorMessage))
	{
		return false;
	}
	UEdGraph* Graph = Entry ? Entry->GetImplementationGraph() : nullptr;
	if (Graph && !Entry->OwnsGraph(Graph))
	{
		ErrorMessage = LOCTEXT("InvalidGraphOwner", "此声明没有独立持有实现图，请重新创建声明");
		return false;
	}
	const FName TargetName(*ExportName);
	if (const UModBlueprintExports* Exports = UModBlueprintExports::Find(Blueprint);
	    Exports && HasExportName(Exports, Entry, TargetName))
	{
		ErrorMessage = LOCTEXT("DuplicateName", "此导出名称已被使用，请输入其他名称");
		return false;
	}
	FKismetNameValidator Validator(Blueprint, Entry ? Entry->GetExportName() : NAME_None);
	const EValidatorResult Result = Validator.IsValid(ExportName);
	if (Result != EValidatorResult::Ok)
	{
		ErrorMessage = INameValidatorInterface::GetErrorText(ExportName, Result);
		return false;
	}
	if (Graph && !Graph->Rename(*ExportName, Graph->GetOuter(), REN_Test))
	{
		ErrorMessage = LOCTEXT("GraphNameUnavailable", "此实现图名称不可用");
		return false;
	}
	ErrorMessage = FText::GetEmpty();
	return true;
}

UEdGraph* CreateImplementationGraph(UBlueprint* Blueprint, UModBlueprintExportEntry* Entry)
{
	UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Entry, Entry->GetExportName(), UEdGraph::StaticClass(),
	                                                        UModBlueprintExportGraphSchema::StaticClass());
	if (Graph)
	{
		Graph->GraphGuid = FGuid::NewGuid();
		Graph->SetFlags(RF_Transactional);
		Graph->bAllowDeletion = false;
		Graph->bAllowRenaming = false;
		Graph->bEditable = true;
		FBlueprintEditorUtils::CreateFunctionGraph(Blueprint, Graph, true, static_cast<UFunction*>(nullptr));
	}
	return Graph;
}

void RenameExportReferences(UBlueprint* Blueprint, const FName OldName, const FName NewName)
{
	for (TObjectIterator<UK2Node_CallFunction> It(RF_ClassDefaultObject | RF_Transient); It; ++It)
	{
		UK2Node_CallFunction* Call = *It;
		if (Call->FunctionReference.GetMemberName() != OldName)
		{
			continue;
		}
		UBlueprint* Caller = FBlueprintEditorUtils::FindBlueprintForNode(Call);
		if (!Caller)
		{
			continue;
		}
		const UClass* ContextClass = Call->FunctionReference.IsSelfContext()
		                                 ? Caller->GeneratedClass.Get()
		                                 : Call->FunctionReference.GetMemberParentClass();
		if (ContextClass && (UBlueprint::GetBlueprintFromClass(ContextClass) == Blueprint ||
		                     (Blueprint->GeneratedClass && ContextClass->IsChildOf(Blueprint->GeneratedClass))))
		{
			Call->Modify();
			if (Call->FunctionReference.IsSelfContext())
			{
				Call->FunctionReference.SetSelfMember(NewName);
			}
			else
			{
				Call->FunctionReference.SetExternalMember(NewName, Call->FunctionReference.GetMemberParentClass());
			}
			FBlueprintEditorUtils::MarkBlueprintAsModified(Caller);
		}
	}
	FBlueprintEditorUtils::ReplaceFunctionReferences(Blueprint, OldName, NewName);
}

void RenameImplementationGraph(UEdGraph* Graph, const FName OldExportName, const FString& NewName)
{
	const FString OldName = Graph->GetName();
	if (OldExportName != Graph->GetFName() && OldExportName != FName(*NewName))
	{
		RenameExportReferences(FBlueprintEditorUtils::FindBlueprintForGraphChecked(Graph), OldExportName,
		                       FName(*NewName));
	}
	TArray<UEdGraph*> Graphs{Graph};
	Graph->GetAllChildrenGraphs(Graphs);
	for (UEdGraph* FunctionGraph : Graphs)
	{
		for (UEdGraphNode* Node : FunctionGraph->Nodes)
		{
			if (UK2Node_Variable* Variable = Cast<UK2Node_Variable>(Node);
			    Variable && Variable->VariableReference.IsLocalScope() &&
			    (Variable->VariableReference.GetMemberScopeName() == OldName ||
			     Variable->VariableReference.GetMemberScopeName() == OldExportName.ToString()))
			{
				Variable->Modify();
				Variable->VariableReference.SetLocalMember(Variable->VariableReference.GetMemberName(), NewName,
				                                           Variable->VariableReference.GetMemberGuid());
			}
		}
	}
	FBlueprintEditorUtils::RenameGraph(Graph, NewName);
	Graph->NotifyGraphChanged();
}

void GenerateFunctionGraph(FKismetCompilerContext* CompilerContext, const UModBlueprintExportEntry* Entry,
                           UEdGraph* SourceGraph)
{
	if (!Entry->OwnsGraph(SourceGraph))
	{
		ReportExportError(CompilerContext->MessageLog, Entry,
		                  LOCTEXT("InvalidImplementationGraph", "声明未独立持有有效的实现图。"));
		return;
	}
	const FName ExportName = Entry->GetExportName();
	const FString FunctionName = ExportName.ToString();
	if (Entry->DisplayName.TrimStartAndEnd().IsEmpty() || ExportName.IsNone() ||
	    FunctionName.Len() > FKismetNameValidator::GetMaximumNameLength() ||
	    !FName::IsValidXName(FunctionName, INVALID_OBJECTNAME_CHARACTERS))
	{
		ReportExportError(CompilerContext->MessageLog, Entry, LOCTEXT("InvalidExportName", "声明名称无效。"));
		return;
	}
	if (const UModBlueprintExports* Exports = UModBlueprintExports::Find(Entry->GetTypedOuter<UBlueprint>());
	    Exports && HasExportName(Exports, Entry, ExportName))
	{
		ReportExportError(CompilerContext->MessageLog, Entry, LOCTEXT("DuplicateExportName", "声明名称重复。"));
		return;
	}
	UEdGraph* GeneratedGraph = CompilerContext->SpawnIntermediateFunctionGraph(FunctionName, nullptr, false);
	if (!GeneratedGraph)
	{
		ReportExportError(CompilerContext->MessageLog, Entry, LOCTEXT("CompileGraphFailed", "无法生成编译函数图。"));
		return;
	}
	const TArray<UEdGraphNode*> DefaultNodes = GeneratedGraph->Nodes;
	for (UEdGraphNode* Node : DefaultNodes)
	{
		GeneratedGraph->RemoveNode(Node, false);
	}
	FEdGraphUtilities::CloneAndMergeGraphIn(GeneratedGraph, SourceGraph, CompilerContext->MessageLog, true, true);
	Entry->ApplyGraphNames(GeneratedGraph, SourceGraph->GetName());
}
} // namespace

FName UModBlueprintExportEntry::GetExportName() const
{
	const FString Name = MakeExportName(GetClass(), DisplayName);
	return Name.Len() < NAME_SIZE ? FName(*Name) : NAME_None;
}

FText UModBlueprintExportEntry::GetExportDisplayName() const
{
	return FText::FromName(GetExportName());
}

FText UModBlueprintExportEntry::GetExportTooltip() const
{
	const FText Name = GetExportDisplayName();
	return Description.IsEmpty() ? Name : FText::Format(LOCTEXT("ExportTooltip", "{0}\n{1}"), Name, Description);
}

void UModBlueprintExportEntry::ApplyGraphNames(UEdGraph* Graph, const FString& SourceScope) const
{
	const FName Name = GetExportName();
	const FString FunctionName = Name.ToString();
	FName PreviousName = NAME_None;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (const UK2Node_FunctionEntry* FunctionEntry = Cast<UK2Node_FunctionEntry>(Node))
		{
			PreviousName = FunctionEntry->FunctionReference.GetMemberName();
			break;
		}
	}
	TArray<UEdGraph*> Graphs{Graph};
	Graph->GetAllChildrenGraphs(Graphs);
	for (UEdGraph* FunctionGraph : Graphs)
	{
		for (UEdGraphNode* Node : FunctionGraph->Nodes)
		{
			if (UK2Node_FunctionEntry* FunctionEntry = Cast<UK2Node_FunctionEntry>(Node))
			{
				FunctionEntry->FunctionReference.SetSelfMember(Name);
				FunctionEntry->CustomGeneratedFunctionName = Name;
				FunctionEntry->MetaData.SetMetaData(FName(TEXT("DisplayName")), FString(FunctionName));
				FunctionEntry->MetaData.ToolTip = GetExportTooltip();
				if (!FunctionEntry->MetaData.CompactNodeTitle.IsEmpty())
				{
					FunctionEntry->MetaData.CompactNodeTitle = GetExportDisplayName();
				}
			}
			else if (UK2Node_FunctionResult* FunctionResult = Cast<UK2Node_FunctionResult>(Node))
			{
				FunctionResult->FunctionReference.SetSelfMember(Name);
			}
			else if (UK2Node_Variable* Variable = Cast<UK2Node_Variable>(Node);
			         Variable && Variable->VariableReference.IsLocalScope() &&
			         (Variable->VariableReference.GetMemberScopeName() == SourceScope ||
			          Variable->VariableReference.GetMemberScopeName() == PreviousName.ToString()))
			{
				Variable->VariableReference.SetLocalMember(Variable->VariableReference.GetMemberName(), FunctionName,
				                                           Variable->VariableReference.GetMemberGuid());
			}
		}
	}
}

UEdGraph* UModBlueprintExportEntry::GetImplementationGraph() const
{
	if (const UModBlueprintAPIExport* API = Cast<UModBlueprintAPIExport>(this))
	{
		return API->ImplementationGraph;
	}
	if (const UModBlueprintHookExport* Hook = Cast<UModBlueprintHookExport>(this))
	{
		return Hook->OriginalGraph;
	}
	return nullptr;
}

bool UModBlueprintExportEntry::CanSetDisplayName(const FString& NewName, FText& ErrorMessage) const
{
	return ValidateDisplayName(GetTypedOuter<UBlueprint>(), GetClass(), this, NewName, ErrorMessage);
}

bool UModBlueprintExportEntry::SetDisplayName(const FString& NewName, FText& ErrorMessage)
{
	if (!CanSetDisplayName(NewName, ErrorMessage))
	{
		return false;
	}
	const FString Name = NormalizeDisplayName(GetClass(), NewName);
	const FString ExportName = MakeExportName(GetClass(), Name);
	const FName OldExportName = GetExportName();
	UEdGraph* Graph = GetImplementationGraph();
	const bool bRenameGraph = Graph && Graph->GetName() != ExportName;
	if (DisplayName == Name && !bRenameGraph)
	{
		return true;
	}
	Modify();
	DisplayName = Name;
	if (bRenameGraph)
	{
		RenameImplementationGraph(Graph, OldExportName, ExportName);
	}
	else
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(GetTypedOuter<UBlueprint>());
	}
	UModBlueprintExports::RefreshSearchIndex(GetTypedOuter<UBlueprint>());
	return true;
}

bool UModBlueprintExportEntry::OwnsGraph(const UEdGraph* Graph) const
{
	const UBlueprint* Blueprint = GetTypedOuter<UBlueprint>();
	return Blueprint && IsValid(Graph) && Graph->GetOuter() == this && !Graph->HasAnyFlags(RF_Transient) &&
	       FBlueprintEditorUtils::FindBlueprintForGraph(Graph) == Blueprint;
}

void UModBlueprintExportEntry::PreEditChange(FProperty* PropertyAboutToChange)
{
	if (PropertyAboutToChange &&
	    PropertyAboutToChange->GetFName() == GET_MEMBER_NAME_CHECKED(UModBlueprintExportEntry, DisplayName))
	{
		PreviousDisplayName = DisplayName;
	}
	Super::PreEditChange(PropertyAboutToChange);
}

void UModBlueprintExportEntry::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UModBlueprintExportEntry, DisplayName))
	{
		const FString NewName = DisplayName;
		DisplayName = PreviousDisplayName;
		FText ErrorMessage;
		if (!SetDisplayName(NewName, ErrorMessage))
		{
			FNotificationInfo Notification(ErrorMessage);
			Notification.ExpireDuration = 5.0f;
			FSlateNotificationManager::Get().AddNotification(Notification);
		}
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.GetPropertyName() != GET_MEMBER_NAME_CHECKED(UModBlueprintExportEntry, DisplayName))
	{
		UModBlueprintExports::RefreshSearchIndex(GetTypedOuter<UBlueprint>());
	}
}

TArray<FString> UModBlueprintAPIExport::GetScopes() const
{
	return Mod::ExportMetadata::FOptions::Load(Mod::ExportMetadata::EOptionKind::APIScope);
}

TArray<FString> UModBlueprintAPIExport::GetThreads() const
{
	return Mod::ExportMetadata::FOptions::Load(Mod::ExportMetadata::EOptionKind::APIThread);
}

TArray<FString> UModBlueprintAPIExport::GetFailureRules() const
{
	return Mod::ExportMetadata::FOptions::Load(Mod::ExportMetadata::EOptionKind::APIFailureRule);
}

TArray<FString> UModBlueprintAPIExport::GetPermissions() const
{
	return Mod::ExportMetadata::FOptions::Load(Mod::ExportMetadata::EOptionKind::APIPermission);
}

TArray<FString> UModBlueprintEventExport::GetSourceScopes() const
{
	return Mod::ExportMetadata::FOptions::Load(Mod::ExportMetadata::EOptionKind::EventSourceScope);
}

TArray<FString> UModBlueprintEventExport::GetPublishRules() const
{
	return Mod::ExportMetadata::FOptions::Load(Mod::ExportMetadata::EOptionKind::EventPublishRule);
}

UModBlueprintExports* UModBlueprintExports::Find(const UBlueprint* Blueprint)
{
	if (!Blueprint)
	{
		return nullptr;
	}
	for (const TObjectPtr<UBlueprintExtension>& Extension : Blueprint->GetExtensions())
	{
		if (UModBlueprintExports* Exports = Cast<UModBlueprintExports>(Extension))
		{
			return Exports;
		}
	}
	return nullptr;
}

UModBlueprintExports* UModBlueprintExports::FindOrAdd(UBlueprint* Blueprint)
{
	if (UModBlueprintExports* Existing = Find(Blueprint))
	{
		return Existing;
	}
	if (!Blueprint)
	{
		return nullptr;
	}
	Blueprint->Modify();
	UModBlueprintExports* Exports = NewObject<UModBlueprintExports>(Blueprint, NAME_None, RF_Transactional);
	Blueprint->AddExtension(Exports);
	return Exports;
}

void UModBlueprintExports::RefreshSearchIndex(UBlueprint* Blueprint)
{
	if (Blueprint && !IsRunningCommandlet())
	{
		FBlueprintActionDatabase::Get().RefreshAssetActions(Blueprint);
	}
	if (Blueprint && Blueprint->IsAsset() && !Blueprint->bBeingCompiled && !Blueprint->bIsRegeneratingOnLoad)
	{
		FFindInBlueprintSearchManager::Get().AddOrUpdateBlueprintSearchMetadata(
		    Blueprint, EAddOrUpdateBlueprintSearchMetadataFlags::ForceRecache |
		                   EAddOrUpdateBlueprintSearchMetadataFlags::ClearCachedValue);
	}
}

bool UModBlueprintExports::CanAddEntry(const UBlueprint* Blueprint, TSubclassOf<UModBlueprintExportEntry> EntryClass,
                                       const FString& DisplayName, FText& ErrorMessage)
{
	const bool bEvent = EntryClass == UModBlueprintEventExport::StaticClass();
	const bool bFunction =
	    EntryClass == UModBlueprintAPIExport::StaticClass() || EntryClass == UModBlueprintHookExport::StaticClass();
	if (!Blueprint || (!bEvent && !bFunction) || Blueprint->BlueprintType == BPTYPE_Interface ||
	    Blueprint->BlueprintType == BPTYPE_MacroLibrary ||
	    (bEvent ? !Blueprint->SupportsEventGraphs() && !Blueprint->SupportsFunctions()
	            : !Blueprint->SupportsFunctions()))
	{
		ErrorMessage = LOCTEXT("UnsupportedBlueprintType", "当前蓝图类型不能创建此类导出声明");
		return false;
	}
	return ValidateDisplayName(Blueprint, EntryClass.Get(), nullptr, DisplayName, ErrorMessage);
}

UModBlueprintAPIExport* UModBlueprintExports::AddAPI(UBlueprint* Blueprint, const FString& DisplayName,
                                                     FText& ErrorMessage)
{
	if (!CanAddEntry(Blueprint, UModBlueprintAPIExport::StaticClass(), DisplayName, ErrorMessage))
	{
		return nullptr;
	}
	Blueprint->Modify();
	Modify();
	UModBlueprintAPIExport* Entry = NewObject<UModBlueprintAPIExport>(this, NAME_None, RF_Transactional);
	Entry->EntryGuid = FGuid::NewGuid();
	Entry->DisplayName = NormalizeDisplayName(Entry->GetClass(), DisplayName);
	Entry->ExportVersion = TEXT("1.0.0");
	Entry->PublicName = Entry->DisplayName;
	Entry->ImplementationGraph = CreateImplementationGraph(Blueprint, Entry);
	if (!Entry->ImplementationGraph)
	{
		ErrorMessage = LOCTEXT("CreateGraphFailed", "无法创建实现图");
		return nullptr;
	}
	APIs.Add(Entry);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	RefreshSearchIndex(Blueprint);
	return Entry;
}

UModBlueprintEventExport* UModBlueprintExports::AddEvent(UBlueprint* Blueprint, const FString& DisplayName,
                                                         FText& ErrorMessage)
{
	if (!CanAddEntry(Blueprint, UModBlueprintEventExport::StaticClass(), DisplayName, ErrorMessage))
	{
		return nullptr;
	}
	Blueprint->Modify();
	Modify();
	UModBlueprintEventExport* Entry = NewObject<UModBlueprintEventExport>(this, NAME_None, RF_Transactional);
	Entry->EntryGuid = FGuid::NewGuid();
	Entry->DisplayName = NormalizeDisplayName(Entry->GetClass(), DisplayName);
	Entry->ExportVersion = TEXT("1.0.0");
	Events.Add(Entry);
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	RefreshSearchIndex(Blueprint);
	return Entry;
}

UModBlueprintHookExport* UModBlueprintExports::AddHook(UBlueprint* Blueprint, const FString& DisplayName,
                                                       FText& ErrorMessage)
{
	if (!CanAddEntry(Blueprint, UModBlueprintHookExport::StaticClass(), DisplayName, ErrorMessage))
	{
		return nullptr;
	}
	Blueprint->Modify();
	Modify();
	UModBlueprintHookExport* Entry = NewObject<UModBlueprintHookExport>(this, NAME_None, RF_Transactional);
	Entry->EntryGuid = FGuid::NewGuid();
	Entry->DisplayName = NormalizeDisplayName(Entry->GetClass(), DisplayName);
	Entry->ExportVersion = TEXT("1.0.0");
	Entry->AllowedStages.Add(EModBlueprintHookStage::Post);
	Entry->OriginalGraph = CreateImplementationGraph(Blueprint, Entry);
	if (!Entry->OriginalGraph)
	{
		ErrorMessage = LOCTEXT("CreateGraphFailed", "无法创建实现图");
		return nullptr;
	}
	Hooks.Add(Entry);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	RefreshSearchIndex(Blueprint);
	return Entry;
}

void UModBlueprintExports::RemoveEntry(UBlueprint* Blueprint, UModBlueprintExportEntry* Entry)
{
	if (!Blueprint || !Entry)
	{
		return;
	}
	if (UModBlueprintAPIExport* API = Cast<UModBlueprintAPIExport>(Entry))
	{
		if (APIs.Contains(API))
		{
			Blueprint->Modify();
			Modify();
			APIs.Remove(API);
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		}
	}
	else if (UModBlueprintHookExport* Hook = Cast<UModBlueprintHookExport>(Entry))
	{
		if (Hooks.Contains(Hook))
		{
			Blueprint->Modify();
			Modify();
			Hooks.Remove(Hook);
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		}
	}
	else if (UModBlueprintEventExport* Event = Cast<UModBlueprintEventExport>(Entry))
	{
		if (Events.Contains(Event))
		{
			Blueprint->Modify();
			Modify();
			Events.Remove(Event);
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		}
	}
	RefreshSearchIndex(Blueprint);
}

void UModBlueprintExports::GetAllGraphs(TArray<UEdGraph*>& Graphs) const
{
	for (const UModBlueprintAPIExport* Entry : APIs)
	{
		if (Entry && Entry->OwnsGraph(Entry->ImplementationGraph) &&
		    UModBlueprintExportGraphSchema::BindToDeclaration(Entry->ImplementationGraph))
		{
			Graphs.Add(Entry->ImplementationGraph);
		}
	}
	for (const UModBlueprintHookExport* Entry : Hooks)
	{
		if (Entry && Entry->OwnsGraph(Entry->OriginalGraph) &&
		    UModBlueprintExportGraphSchema::BindToDeclaration(Entry->OriginalGraph))
		{
			Graphs.Add(Entry->OriginalGraph);
		}
	}
}

void UModBlueprintExports::HandleGenerateFunctionGraphs(FKismetCompilerContext* CompilerContext)
{
	for (const UModBlueprintAPIExport* Entry : APIs)
	{
		if (Entry)
		{
			GenerateFunctionGraph(CompilerContext, Entry, Entry->ImplementationGraph);
		}
	}
	for (const UModBlueprintHookExport* Entry : Hooks)
	{
		if (Entry && ValidateHookPolicy(CompilerContext->MessageLog, Entry))
		{
			GenerateFunctionGraph(CompilerContext, Entry, Entry->OriginalGraph);
		}
	}
}

UModBlueprintExports::FSearchData UModBlueprintExports::HandleGatherSearchData(const UBlueprint* OwningBlueprint) const
{
	FSearchData Data;
	if (OwningBlueprint != GetTypedOuter<UBlueprint>())
	{
		return Data;
	}
	Data.Datas.Emplace(FFindInBlueprintSearchTags::FiB_Name, LOCTEXT("ExportsSearchName", "蓝图Mod导出"));
	const auto AddGroup = [&Data](const FText& Title, const auto& Entries)
	{
		TUniquePtr<FSearchArrayData> Group = MakeUnique<FSearchArrayData>();
		Group->Identifier = Title;
		for (const auto& Entry : Entries)
		{
			if (Entry)
			{
				FSearchData& Item = Group->SearchSubList.AddDefaulted_GetRef();
				Item.Datas.Emplace(FFindInBlueprintSearchTags::FiB_Name, Entry->GetExportDisplayName());
				Item.Datas.Emplace(FFindInBlueprintSearchTags::FiB_Tooltip, Entry->GetExportTooltip());
			}
		}
		Data.SearchArrayDatas.Add(MoveTemp(Group));
	};
	AddGroup(LOCTEXT("APISearchGroup", "API"), APIs);
	AddGroup(LOCTEXT("EventSearchGroup", "Event"), Events);
	AddGroup(LOCTEXT("HookSearchGroup", "Hook"), Hooks);
	return Data;
}

#undef LOCTEXT_NAMESPACE
