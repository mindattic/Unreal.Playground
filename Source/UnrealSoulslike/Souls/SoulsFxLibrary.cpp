// UNR-T51 Editor helper: tunes the lightweight (stateless) Niagara emitters of the torch fire.
#include "SoulsFxLibrary.h"
#include "UnrealSoulslike.h"

#if WITH_EDITOR
#include "Curves/RichCurve.h"
#include "NiagaraEmitterBase.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraSystem.h"
#include "Stateless/NiagaraStatelessDistribution.h"
#include "UObject/UnrealType.h"

namespace SoulsFx
{
	UObject* StatelessEmitterOf(UObject* SystemObject)
	{
		UNiagaraSystem* System = Cast<UNiagaraSystem>(SystemObject);
		if (!System)
		{
			return nullptr;
		}
		for (FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			if (Handle.GetEmitterMode() == ENiagaraEmitterMode::Stateless)
			{
				if (UNiagaraEmitterBase* Emitter = Handle.GetEmitterBase())
				{
					return Emitter;
				}
			}
		}
		return nullptr;
	}

	// The objects in an object-array property (the emitter's Modules / RendererProperties).
	TArray<UObject*> ObjectsIn(UObject* Owner, const TCHAR* PropertyName)
	{
		TArray<UObject*> Out;
		const FArrayProperty* Array = Owner ? FindFProperty<FArrayProperty>(Owner->GetClass(), PropertyName) : nullptr;
		const FObjectPropertyBase* Inner = Array ? CastField<FObjectPropertyBase>(Array->Inner) : nullptr;
		if (!Inner)
		{
			return Out;
		}
		FScriptArrayHelper Helper(Array, Array->ContainerPtrToValuePtr<void>(Owner));
		for (int32 Index = 0; Index < Helper.Num(); ++Index)
		{
			Out.Add(Inner->GetObjectPropertyValue(Helper.GetRawPtr(Index)));
		}
		return Out;
	}

	UObject* FindTarget(UObject* SystemObject, const FString& Target)
	{
		UObject* Emitter = StatelessEmitterOf(SystemObject);
		if (!Emitter)
		{
			return nullptr;
		}
		if (Target == TEXT("Emitter"))
		{
			return Emitter;
		}
		if (Target == TEXT("SpriteRenderer"))
		{
			for (UObject* Renderer : ObjectsIn(Emitter, TEXT("RendererProperties")))
			{
				if (Renderer && Renderer->GetClass()->GetName().Contains(TEXT("SpriteRenderer")))
				{
					return Renderer;
				}
			}
			return nullptr;
		}
		const FString ClassName = FString(TEXT("NiagaraStatelessModule_")) + Target;
		for (UObject* Module : ObjectsIn(Emitter, TEXT("Modules")))
		{
			if (Module && Module->GetClass()->GetName() == ClassName)
			{
				return Module;
			}
		}
		return nullptr;
	}

	// Walks "A.B[2].C" from Object to the last property and the address of its value.
	bool Resolve(UObject* Object, const FString& Path, FProperty*& OutProperty, void*& OutValue)
	{
		TArray<FString> Parts;
		Path.ParseIntoArray(Parts, TEXT("."));
		const UStruct* Struct = Object->GetClass();
		void* Container = Object;
		for (int32 PartIndex = 0; PartIndex < Parts.Num(); ++PartIndex)
		{
			FString Name = Parts[PartIndex];
			int32 ArrayIndex = INDEX_NONE;
			int32 Bracket = INDEX_NONE;
			if (Name.FindChar(TEXT('['), Bracket))
			{
				ArrayIndex = FCString::Atoi(*Name.Mid(Bracket + 1));
				Name.LeftInline(Bracket);
			}
			FProperty* Property = FindFProperty<FProperty>(Struct, FName(*Name));
			if (!Property)
			{
				return false;
			}
			void* Value = Property->ContainerPtrToValuePtr<void>(Container);
			if (ArrayIndex != INDEX_NONE)
			{
				FArrayProperty* Array = CastField<FArrayProperty>(Property);
				if (!Array)
				{
					return false;
				}
				FScriptArrayHelper Helper(Array, Value);
				if (!Helper.IsValidIndex(ArrayIndex))
				{
					return false;
				}
				Value = Helper.GetRawPtr(ArrayIndex);
				Property = Array->Inner;
			}
			if (PartIndex == Parts.Num() - 1)
			{
				OutProperty = Property;
				OutValue = Value;
				return true;
			}
			FStructProperty* StructProperty = CastField<FStructProperty>(Property);
			if (!StructProperty)
			{
				return false;
			}
			Struct = StructProperty->Struct;
			Container = Value;
		}
		return false;
	}

	bool ParseMode(const FString& Text, ENiagaraDistributionMode& OutMode)
	{
		struct FEntry { const TCHAR* Name; ENiagaraDistributionMode Mode; };
		static const FEntry Entries[] = {
			{ TEXT("UniformConstant"), ENiagaraDistributionMode::UniformConstant },
			{ TEXT("NonUniformConstant"), ENiagaraDistributionMode::NonUniformConstant },
			{ TEXT("UniformRange"), ENiagaraDistributionMode::UniformRange },
			{ TEXT("NonUniformRange"), ENiagaraDistributionMode::NonUniformRange },
			{ TEXT("UniformCurve"), ENiagaraDistributionMode::UniformCurve },
			{ TEXT("NonUniformCurve"), ENiagaraDistributionMode::NonUniformCurve },
			{ TEXT("ColorGradient"), ENiagaraDistributionMode::ColorGradient },
		};
		for (const FEntry& Entry : Entries)
		{
			if (Text.Equals(Entry.Name, ESearchCase::IgnoreCase))
			{
				OutMode = Entry.Mode;
				return true;
			}
		}
		return false;
	}

	bool Fail(const FString& Target, const FString& Path, const TCHAR* Why)
	{
		UE_LOG(LogUnrealSoulslike, Warning, TEXT("SoulsFx: %s.%s: %s"), *Target, *Path, Why);
		return false;
	}
}
#endif // WITH_EDITOR

TArray<FString> USoulsFxLibrary::ListFxModules(UObject* System)
{
	TArray<FString> Out;
#if WITH_EDITOR
	UObject* Emitter = SoulsFx::StatelessEmitterOf(System);
	if (!Emitter)
	{
		Out.Add(TEXT("no stateless emitter"));
		return Out;
	}
	for (UObject* Module : SoulsFx::ObjectsIn(Emitter, TEXT("Modules")))
	{
		if (!Module)
		{
			continue;
		}
		FString Enabled;
		FProperty* Property = nullptr;
		void* Value = nullptr;
		if (SoulsFx::Resolve(Module, TEXT("bModuleEnabled"), Property, Value))
		{
			Property->ExportTextItem_Direct(Enabled, Value, nullptr, Module, PPF_None);
		}
		Out.Add(FString::Printf(TEXT("%s %s"), *Module->GetClass()->GetName().Replace(TEXT("NiagaraStatelessModule_"), TEXT("")), *Enabled));
	}
	for (UObject* Renderer : SoulsFx::ObjectsIn(Emitter, TEXT("RendererProperties")))
	{
		if (Renderer)
		{
			Out.Add(FString::Printf(TEXT("renderer %s"), *Renderer->GetClass()->GetName()));
		}
	}
#endif
	return Out;
}

bool USoulsFxLibrary::SetFxValue(UObject* System, const FString& Target, const FString& Path, const FString& Text)
{
#if WITH_EDITOR
	UObject* Object = SoulsFx::FindTarget(System, Target);
	if (!Object)
	{
		return SoulsFx::Fail(Target, Path, TEXT("no such target"));
	}
	FProperty* Property = nullptr;
	void* Value = nullptr;
	if (!SoulsFx::Resolve(Object, Path, Property, Value))
	{
		return SoulsFx::Fail(Target, Path, TEXT("no such property"));
	}
	Object->Modify();
	if (!Property->ImportText_Direct(*Text, Value, Object, PPF_None))
	{
		return SoulsFx::Fail(Target, Path, TEXT("value did not parse"));
	}
	return true;
#else
	return false;
#endif
}

FString USoulsFxLibrary::GetFxValue(UObject* System, const FString& Target, const FString& Path)
{
	FString Text;
#if WITH_EDITOR
	UObject* Object = SoulsFx::FindTarget(System, Target);
	FProperty* Property = nullptr;
	void* Value = nullptr;
	if (Object && SoulsFx::Resolve(Object, Path, Property, Value))
	{
		Property->ExportTextItem_Direct(Text, Value, nullptr, Object, PPF_None);
	}
#endif
	return Text;
}

bool USoulsFxLibrary::SetFxDistribution(UObject* System, const FString& Target, const FString& Path, const FString& Mode, const TArray<float>& Values)
{
#if WITH_EDITOR
	UObject* Object = SoulsFx::FindTarget(System, Target);
	if (!Object)
	{
		return SoulsFx::Fail(Target, Path, TEXT("no such target"));
	}
	FProperty* Property = nullptr;
	void* Value = nullptr;
	if (!SoulsFx::Resolve(Object, Path, Property, Value))
	{
		return SoulsFx::Fail(Target, Path, TEXT("no such property"));
	}
	const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
	if (!StructProperty || !StructProperty->Struct->IsChildOf(FNiagaraDistributionBase::StaticStruct()))
	{
		return SoulsFx::Fail(Target, Path, TEXT("not a Niagara distribution"));
	}
	ENiagaraDistributionMode NewMode = ENiagaraDistributionMode::UniformConstant;
	if (!SoulsFx::ParseMode(Mode, NewMode))
	{
		return SoulsFx::Fail(Target, Path, TEXT("unknown distribution mode"));
	}
	// Every FNiagaraDistribution* derives (singly) from FNiagaraDistributionBase.
	FNiagaraDistributionBase* Distribution = static_cast<FNiagaraDistributionBase*>(Value);
	const int32 Channels = FMath::Max(Distribution->GetBaseNumberOfChannels(), 1);
	const bool bCurve = NewMode == ENiagaraDistributionMode::UniformCurve || NewMode == ENiagaraDistributionMode::NonUniformCurve
		|| NewMode == ENiagaraDistributionMode::ColorGradient;
	const bool bUniform = NewMode == ENiagaraDistributionMode::UniformConstant || NewMode == ENiagaraDistributionMode::UniformRange
		|| NewMode == ENiagaraDistributionMode::UniformCurve;
	const int32 Width = bUniform ? 1 : Channels;

	Object->Modify();
	Distribution->Mode = NewMode;
	Distribution->ChannelCurves.Reset();
	Distribution->ChannelConstantsAndRanges.Reset();
	if (bCurve)
	{
		const int32 Stride = 1 + Width;
		if (Values.Num() < Stride * 2 || Values.Num() % Stride != 0)
		{
			return SoulsFx::Fail(Target, Path, TEXT("curve needs at least two keys of [time, value per channel]"));
		}
		Distribution->ChannelCurves.SetNum(Width);
		for (int32 Key = 0; Key < Values.Num(); Key += Stride)
		{
			for (int32 Channel = 0; Channel < Width; ++Channel)
			{
				FRichCurve& Curve = Distribution->ChannelCurves[Channel];
				const FKeyHandle Handle = Curve.AddKey(Values[Key], Values[Key + 1 + Channel]);
				Curve.SetKeyInterpMode(Handle, RCIM_Linear);
			}
		}
	}
	else
	{
		const bool bRange = NewMode == ENiagaraDistributionMode::UniformRange || NewMode == ENiagaraDistributionMode::NonUniformRange;
		const int32 Expected = bRange ? Width * 2 : Width;
		if (Values.Num() != Expected)
		{
			return SoulsFx::Fail(Target, Path, TEXT("wrong number of values for the mode"));
		}
		Distribution->ChannelConstantsAndRanges = Values;
	}
	// Bakes Values / Min / Max (what the simulation reads) from the editor data above.
	Distribution->UpdateValuesFromDistribution();
	return true;
#else
	return false;
#endif
}

bool USoulsFxLibrary::FinishFx(UObject* SystemObject)
{
#if WITH_EDITOR
	UNiagaraSystem* System = Cast<UNiagaraSystem>(SystemObject);
	UObject* Emitter = SoulsFx::StatelessEmitterOf(SystemObject);
	if (!System || !Emitter)
	{
		return false;
	}
	for (UObject* Module : SoulsFx::ObjectsIn(Emitter, TEXT("Modules")))
	{
		if (Module)
		{
			Module->PostEditChange();
		}
	}
	for (UObject* Renderer : SoulsFx::ObjectsIn(Emitter, TEXT("RendererProperties")))
	{
		if (Renderer)
		{
			Renderer->PostEditChange();
		}
	}
	Emitter->PostEditChange();
	System->PostEditChange();
	System->RequestCompile(false);
	System->WaitForCompilationComplete(true, false);
	System->MarkPackageDirty();
	return true;
#else
	return false;
#endif
}
