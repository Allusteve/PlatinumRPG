// Copyright 2022 UNAmedia. All Rights Reserved.

#include "MixamoAnimationRootMotionSolver.h"

#include "MixamoToolkitPrivatePCH.h"

#include "MixamoToolkitPrivate.h"
#include "MixamoSkeletonRetargeter.h"

#include "Editor.h"
#include "SMixamoToolkitWidget.h"
#include "Misc/MessageDialog.h"
#include <Logging/MessageLog.h>

#include "Animation/AnimSequence.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"

#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"

#define LOCTEXT_NAMESPACE "FMixamoAnimationRetargetingModule"

void FMixamoAnimationRootMotionSolver::LaunchProcedureFlow(USkeleton* Skeleton)
{
    checkf(Skeleton != nullptr, TEXT("A reference skeleton must be specified."));
    checkf(CanExecuteProcedure(Skeleton), TEXT("Incompatible skeleton."));

    TSharedRef<SWindow> WidgetWindow = SNew(SWindow)
        .Title(LOCTEXT("FMixamoAnimationRootMotionSolver_AskUserForAnimations_WindowTitle", "Select animations"))
        .ClientSize(FVector2D(1000, 600))
        .SupportsMinimize(false)
        .SupportsMaximize(false)
        .HasCloseButton(false);

    TSharedRef<SRootMotionExtractionWidget> RootMotionExtractionWidget = SNew(SRootMotionExtractionWidget)
        .ReferenceSkeleton(Skeleton);

    WidgetWindow->SetContent(RootMotionExtractionWidget);

    GEditor->EditorAddModalWindow(WidgetWindow);

    UAnimSequence* SelectedAnimation = RootMotionExtractionWidget->GetSelectedAnimation();
    UAnimSequence* SelectedInPlaceAnimation = RootMotionExtractionWidget->GetSelectedInPlaceAnimation();

    if (!SelectedAnimation || !SelectedInPlaceAnimation)
        return;

    // check, with an heuristic, that the user has selected the right "IN PLACE" animation otherwise prompt a message box as warning
    const UAnimSequence* EstimatedInPlaceAnim = EstimateInPlaceAnimation(SelectedAnimation, SelectedInPlaceAnimation);
    if (EstimatedInPlaceAnim != SelectedInPlaceAnimation)
    {
        FText WarningText = LOCTEXT("SRootMotionExtractionWidget_InPlaceAnimWarning", "Warning: are you sure to have choose the right IN PLACE animation?");
        if (FMessageDialog::Open(EAppMsgType::YesNo, WarningText) == EAppReturnType::No)
            return;
    }

    static const FName NAME_AssetTools = "AssetTools";
    IAssetTools* AssetTools = &FModuleManager::GetModuleChecked<FAssetToolsModule>(NAME_AssetTools).Get();

    const FString ResultAnimationName = SelectedAnimation->GetName() + "_rootmotion";
    const FString PackagePath = FAssetData(SelectedAnimation).PackagePath.ToString();
    UAnimSequence* ResultAnimation = Cast<UAnimSequence>(AssetTools->DuplicateAsset(ResultAnimationName, PackagePath, SelectedAnimation));
    if (!ResultAnimation)
    {
        FMessageLog("LogMixamoToolkit").Error(FText::FromString(TEXT("Aborted: failed to duplicate the animation sequence.")));
        return;
    }

    if (ExecuteExtraction(ResultAnimation, SelectedInPlaceAnimation))
    {
        ResultAnimation->bEnableRootMotion = true;

        // focus the content browser on the new animation
        FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
        TArray<UObject*> SyncObjects;
        SyncObjects.Add(ResultAnimation);
        ContentBrowserModule.Get().SyncBrowserToAssets(SyncObjects);
    }
    else 
    {
        FText WarningText = LOCTEXT("SRootMotionExtractionWidget_ExtractionFailedMsg", "Root motion extraction has failed, please double check the input animation sequences (ordinary and inplace). See console for additional details.");
        FMessageDialog::Open(EAppMsgType::Ok, WarningText);

        ResultAnimation->MarkAsGarbage();
    }
}



bool FMixamoAnimationRootMotionSolver::CanExecuteProcedure(const USkeleton* Skeleton) const
{
    // Check the asset content.
    // NOTE: this will load the asset if needed.
    if (!FMixamoAnimationRetargetingModule::Get().GetMixamoSkeletonRetargeter()->IsMixamoSkeleton(Skeleton))
    {
        return false;
    }

    // Check that the skeleton was processed with our retargeter !
    int32 RootBoneIndex = Skeleton->GetReferenceSkeleton().FindBoneIndex(TEXT("root"));
    if (RootBoneIndex == INDEX_NONE)
    {
        return false;
    }

    return true;
}



bool FMixamoAnimationRootMotionSolver::ExecuteExtraction(UAnimSequence* AnimSequence, const UAnimSequence* InPlaceAnimSequence)
{
    check(AnimSequence != InPlaceAnimSequence);

    IAnimationDataModel* AnimDataModel = AnimSequence->GetDataModel();
    IAnimationDataModel* InPlaceAnimDataModel = InPlaceAnimSequence->GetDataModel();

    const FName HipsBoneName = FName("Hips");
    
    if (!AnimDataModel->IsValidBoneTrackName(HipsBoneName))
    {
        FMessageLog("LogMixamoToolkit").Error(FText::FromString(TEXT("Hips bone not found in the ordinary animation sequence.")));
        return false;
    }

    if (!InPlaceAnimDataModel->IsValidBoneTrackName(HipsBoneName))
    {
        FMessageLog("LogMixamoToolkit").Error(FText::FromString(TEXT("Hips bone not found in the inplace animation sequence.")));
        return false;
    }

    TArray<FTransform> HipsBoneTransforms;
    TArray<FTransform> InPlaceHipsBoneTransforms;

    TArray<FName> BoneNames;
    AnimDataModel->GetBoneTrackNames(BoneNames);

    TMap<FName, FRawAnimSequenceTrack> BoneTracksCopy;
    TArray<FTransform> TempTransforms;

    // make a copy of all bone tracks
    for (FName BoneName : BoneNames)
    {
        TempTransforms.Empty(TempTransforms.Num());
        AnimDataModel->GetBoneTrackTransforms(BoneName, TempTransforms);

        FRawAnimSequenceTrack AnimTrack;

        const int32 NumOfKeys = TempTransforms.Num();
        AnimTrack.PosKeys.SetNum(NumOfKeys);
        AnimTrack.RotKeys.SetNum(NumOfKeys);
        AnimTrack.ScaleKeys.SetNum(NumOfKeys);

        for (int i = 0; i < NumOfKeys; ++i)
        {
            AnimTrack.PosKeys[i] = FVector3f(TempTransforms[i].GetLocation());
            AnimTrack.RotKeys[i] = FQuat4f(TempTransforms[i].GetRotation());
            AnimTrack.ScaleKeys[i] = FVector3f(TempTransforms[i].GetScale3D());
        }

        BoneTracksCopy.Add(BoneName, MoveTemp(AnimTrack));
    }

    AnimDataModel->GetBoneTrackTransforms(HipsBoneName, HipsBoneTransforms);
    InPlaceAnimDataModel->GetBoneTrackTransforms(HipsBoneName, InPlaceHipsBoneTransforms);
    
    // nummber of keys should match between the two animations.
    if (HipsBoneTransforms.Num() != InPlaceHipsBoneTransforms.Num())
    {
        FMessageLog("LogMixamoToolkit").Error(FText::FromString(TEXT("Track data keys number mismatch between ordinary and inplace animation sequences.")));
        return false;
    }

    // make a new track for the root bone
    // the keys num is equal to the hips keys num
    FRawAnimSequenceTrack NewRootBoneTrack;
    FRawAnimSequenceTrack NewHipsBoneTrack;

    const int32 NumOfKeys = HipsBoneTransforms.Num();
    NewRootBoneTrack.PosKeys.SetNum(NumOfKeys);
    NewRootBoneTrack.RotKeys.SetNum(NumOfKeys);
    NewRootBoneTrack.ScaleKeys.SetNum(NumOfKeys);
    NewHipsBoneTrack.PosKeys.SetNum(NumOfKeys);
    NewHipsBoneTrack.RotKeys.SetNum(NumOfKeys);
    NewHipsBoneTrack.ScaleKeys.SetNum(NumOfKeys);

    // HipsBoneTrack = Root + Hips
    // InPlaceHipsBoneTrack = Hips
    // we want to extract the Root value and set to the new root track so:
    // Root = HipsBoneTrack - InPlaceHipsBoneTrack = (Root + Hips) - Hips = Root
    for (int i = 0; i < NumOfKeys; ++i)
    {
        NewHipsBoneTrack.PosKeys[i] = FVector3f(InPlaceHipsBoneTransforms[i].GetLocation());
        NewHipsBoneTrack.RotKeys[i] = FQuat4f(InPlaceHipsBoneTransforms[i].GetRotation());
        NewHipsBoneTrack.ScaleKeys[i] = FVector3f(InPlaceHipsBoneTransforms[i].GetScale3D());

        NewRootBoneTrack.PosKeys[i] = FVector3f(HipsBoneTransforms[i].GetLocation()) - NewHipsBoneTrack.PosKeys[i];
        NewRootBoneTrack.RotKeys[i] = FQuat4f(HipsBoneTransforms[i].GetRotation()) * NewHipsBoneTrack.RotKeys[i].Inverse();
        NewRootBoneTrack.ScaleKeys[i] = FVector3f(HipsBoneTransforms[i].GetScale3D());
    }

    // replace hips bone track (it will be used later). 
    BoneTracksCopy[HipsBoneName] = NewHipsBoneTrack;

    IAnimationDataController& Controller = AnimSequence->GetController();

    constexpr bool bShouldTransact = false;
    // NOTE: modifications MUST be done inside a "bracket", otherwise each modification will fire a re-build of the animation.
    // After adding the "root" track, the re-build will fail since its track keys are missing.
    // Worst: there's a bug in UE5.0 (https://github.com/EpicGames/UnrealEngine/blob/05ce24e3038cb1994a7c71d4d0058dbdb112f52b/Engine/Source/Runtime/Engine/Private/Animation/AnimSequenceHelpers.cpp#L593)
    // where when no keys are present, element at index -1 is removed from an array, causing a random memory overriding.
    IAnimationDataController::FScopedBracket ScopedBracket(Controller, LOCTEXT("FMixamoAnimationRootMotionSolver_ExecuteExtraction_AnimEdit", "Animation editing"));

    // remove all bone tracks because we have to insert the "root bone track" at index 0
    // and the AddBoneCurve API doesn't allow to pass the desired index.
    // The ore bone tracks will be inserted after.
    Controller.RemoveAllBoneTracks(bShouldTransact);

    // add the new root track (now as the first item)
    verify(Controller.AddBoneCurve(FName("root"), bShouldTransact));
    verify(Controller.SetBoneTrackKeys(FName("root"), NewRootBoneTrack.PosKeys, NewRootBoneTrack.RotKeys, NewRootBoneTrack.ScaleKeys, bShouldTransact));

    // re-insert all other bone tracks.
    for (FName BoneName : BoneNames)
    {
        const FRawAnimSequenceTrack& BoneAnimTrack = BoneTracksCopy[BoneName];

        verify(Controller.AddBoneCurve(BoneName, bShouldTransact));

        verify(Controller.SetBoneTrackKeys(BoneName,
            BoneAnimTrack.PosKeys, 
            BoneAnimTrack.RotKeys, 
            BoneAnimTrack.ScaleKeys, 
            bShouldTransact));
    }

    // now we can replace the HipsBoneTrack with InPlaceHipsBoneTrack
    // (this is not more needed becase BoneTracksCopy contains already the updated track data.)
    //Controller.SetBoneTrackKeys(HipsBoneName, NewHipsBoneTrack.PosKeys, NewHipsBoneTrack.RotKeys, NewHipsBoneTrack.ScaleKeys, bShouldTransact);

    return true;
}



float FMixamoAnimationRootMotionSolver::GetMaxBoneDisplacement(const UAnimSequence* AnimSequence, const FName& BoneName)
{
    IAnimationDataModel* AnimDataModel = AnimSequence->GetDataModel();

    TArray<FTransform> BoneTransforms;
    AnimDataModel->GetBoneTrackTransforms(BoneName, BoneTransforms);

    float MaxSize = 0;

    for (int i = 0; i < BoneTransforms.Num(); ++i)
    {
        float Size = BoneTransforms[i].GetLocation().Size();
        if (Size > MaxSize)
            MaxSize = Size;
    }

    return MaxSize;
}



const UAnimSequence* FMixamoAnimationRootMotionSolver::EstimateInPlaceAnimation(const UAnimSequence* AnimationA, const UAnimSequence* AnimationB)
{
    FName RefBoneName(TEXT("Hips"));

    /*
    Find the "in place" animation sequence. To do that we compare the two hips bone displacements.
    The animation sequence with the lower value is the "in place" one.
    @TODO: is this checks always reliable ?
    */
    float dA = GetMaxBoneDisplacement(AnimationA, RefBoneName);
    float dB = GetMaxBoneDisplacement(AnimationB, RefBoneName);

    const UAnimSequence* NormalAnimSequence = (dA < dB) ? AnimationB : AnimationA;
    const UAnimSequence* InPlaceAnimSequence = (dA < dB) ? AnimationA : AnimationB;

    return InPlaceAnimSequence;
}
