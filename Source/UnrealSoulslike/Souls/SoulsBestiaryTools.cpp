#include "SoulsBestiaryTools.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#if WITH_EDITOR
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#endif

int32 USoulsBestiaryTools::BakeFootIKBones(UAnimSequence* Sequence)
{
#if WITH_EDITOR
	if (!Sequence || !Sequence->GetSkeleton())
	{
		return 0;
	}
	const FReferenceSkeleton& Reference = Sequence->GetSkeleton()->GetReferenceSkeleton();
	const IAnimationDataModel* Model = Sequence->GetDataModel();
	const int32 IkRoot = Reference.FindBoneIndex(TEXT("ik_foot_root"));
	if (!Model || IkRoot == INDEX_NONE)
	{
		return 0;
	}
	const int32 NumKeys = FMath::Max(Model->GetNumberOfKeys(), 1);
	const TArray<FTransform>& RefPose = Reference.GetRefBonePose();

	// Each bone's local track (or its reference pose where the clip doesn't key it).
	TMap<int32, TArray<FTransform>> Tracks;
	auto LocalAt = [&](int32 Bone, int32 Key) -> FTransform
	{
		if (!Tracks.Contains(Bone))
		{
			TArray<FTransform> Keys;
			const FName Name = Reference.GetBoneName(Bone);
			if (Model->IsValidBoneTrackName(Name))
			{
				Model->GetBoneTrackTransforms(Name, Keys);
			}
			Tracks.Add(Bone, MoveTemp(Keys));
		}
		const TArray<FTransform>& Keys = Tracks.FindChecked(Bone);
		return Keys.IsValidIndex(Key) ? Keys[Key] : RefPose[Bone];
	};
	auto ComponentAt = [&](int32 Bone, int32 Key) -> FTransform
	{
		FTransform Out = FTransform::Identity;
		for (int32 Walk = Bone; Walk != INDEX_NONE; Walk = Reference.GetParentIndex(Walk))
		{
			Out = Out * LocalAt(Walk, Key);
		}
		return Out;
	};

	IAnimationDataController& Controller = Sequence->GetController();
	Controller.OpenBracket(FText::FromString(TEXT("Bake foot IK bones")), false);
	auto Write = [&](FName Name, const TArray<FTransform>& Keys) -> bool
	{
		if (!Model->IsValidBoneTrackName(Name))
		{
			Controller.AddBoneCurve(Name, false);
		}
		TArray<FVector3f> Positions;
		TArray<FQuat4f> Rotations;
		TArray<FVector3f> Scales;
		for (const FTransform& Key : Keys)
		{
			Positions.Add(FVector3f(Key.GetTranslation()));
			Rotations.Add(FQuat4f(Key.GetRotation()));
			Scales.Add(FVector3f(Key.GetScale3D()));
		}
		return Controller.SetBoneTrackKeys(Name, Positions, Rotations, Scales, false);
	};

	int32 Written = 0;
	TArray<FTransform> RootKeys;
	RootKeys.Init(RefPose[IkRoot], NumKeys);
	Written += Write(TEXT("ik_foot_root"), RootKeys) ? 1 : 0;
	for (const TCHAR* Side : { TEXT("l"), TEXT("r") })
	{
		const int32 Foot = Reference.FindBoneIndex(FName(*FString::Printf(TEXT("foot_%s"), Side)));
		const FName IkName(*FString::Printf(TEXT("ik_foot_%s"), Side));
		const int32 Ik = Reference.FindBoneIndex(IkName);
		if (Foot == INDEX_NONE || Ik == INDEX_NONE || Reference.GetParentIndex(Ik) != IkRoot)
		{
			continue;
		}
		TArray<FTransform> Keys;
		Keys.Reserve(NumKeys);
		for (int32 Key = 0; Key < NumKeys; ++Key)
		{
			// ik_foot_root keeps its reference pose under root, so place ik_foot_<side> relative to it.
			const FTransform IkRootComponent = RefPose[IkRoot] * ComponentAt(Reference.GetParentIndex(IkRoot), Key);
			FTransform Local = ComponentAt(Foot, Key).GetRelativeTransform(IkRootComponent);
			Local.SetScale3D(FVector::OneVector);
			Keys.Add(Local);
		}
		Written += Write(IkName, Keys) ? 1 : 0;
	}
	Controller.CloseBracket(false);
	Sequence->MarkPackageDirty();
	return Written;
#else
	return 0;
#endif
}
