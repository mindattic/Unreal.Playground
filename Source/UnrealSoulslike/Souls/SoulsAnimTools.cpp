#include "SoulsAnimTools.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#if WITH_EDITOR
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#endif

int32 USoulsAnimTools::BakeBoneOffsets(UAnimSequence* Sequence, const TArray<FName>& Bones, const TArray<FRotator>& Offsets)
{
#if WITH_EDITOR
	if (!Sequence || Bones.Num() != Offsets.Num() || !Sequence->GetSkeleton())
	{
		return 0;
	}
	const FReferenceSkeleton& Reference = Sequence->GetSkeleton()->GetReferenceSkeleton();
	const IAnimationDataModel* Model = Sequence->GetDataModel();
	if (!Model)
	{
		return 0;
	}
	const int32 NumKeys = FMath::Max(Model->GetNumberOfKeys(), 1);
	IAnimationDataController& Controller = Sequence->GetController();
	Controller.OpenBracket(FText::FromString(TEXT("Bake grip")), false);
	int32 Changed = 0;
	for (int32 Index = 0; Index < Bones.Num(); ++Index)
	{
		const FName Bone = Bones[Index];
		const int32 BoneIndex = Reference.FindBoneIndex(Bone);
		if (BoneIndex == INDEX_NONE)
		{
			continue;
		}
		const FTransform RefPose = Reference.GetRefBonePose()[BoneIndex];
		TArray<FTransform> Existing;
		if (Model->IsValidBoneTrackName(Bone))
		{
			Model->GetBoneTrackTransforms(Bone, Existing);
		}
		else
		{
			Controller.AddBoneCurve(Bone, false);
		}
		const FQuat Rotation = RefPose.GetRotation() * Offsets[Index].Quaternion();
		TArray<FVector3f> Positions;
		TArray<FQuat4f> Rotations;
		TArray<FVector3f> Scales;
		for (int32 Key = 0; Key < NumKeys; ++Key)
		{
			const FTransform& Source = Existing.IsValidIndex(Key) ? Existing[Key] : RefPose;
			Positions.Add(FVector3f(Source.GetTranslation()));
			Rotations.Add(FQuat4f(Rotation));
			Scales.Add(FVector3f(Source.GetScale3D()));
		}
		if (Controller.SetBoneTrackKeys(Bone, Positions, Rotations, Scales, false))
		{
			++Changed;
		}
	}
	Controller.CloseBracket(false);
	Sequence->MarkPackageDirty();
	return Changed;
#else
	return 0;
#endif
}
