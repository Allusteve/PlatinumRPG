// Copyright 2022 UNAmedia. All Rights Reserved.

#include "MixamoToolkitEditorIntegration.h"
#include "MixamoToolkitPrivatePCH.h"

#include "MixamoAnimationRootMotionSolver.h"
#include "MixamoSkeletonRetargeter.h"

#include "MixamoToolkitCommands.h"
#include "MixamoToolkitStyle.h"

#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"

//#include "LevelEditor.h"
#include "Editor/ContentBrowser/Public/ContentBrowserModule.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#include "IPluginWardenModule.h"



#define LOCTEXT_NAMESPACE "FMixamoAnimationRetargetingModule"



void FMixamoToolkitEditorIntegration::Register()
{
	// Register RetargetMixamoSkeleton action into the Content Browser contextual menu.
	// The contextual menu is built at run-time using the specified delegate.
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

		TArray<FContentBrowserMenuExtender_SelectedAssets> & CBMenuExtenderDelegates = ContentBrowserModule.GetAllAssetViewContextMenuExtenders();
		CBMenuExtenderDelegates.Add(FContentBrowserMenuExtender_SelectedAssets::CreateSP(this, &FMixamoToolkitEditorIntegration::MakeContentBrowserContextMenuExtender));
	}
}



void FMixamoToolkitEditorIntegration::Unregister()
{
}



FText FMixamoToolkitEditorIntegration::TooltipGetter_RetargetMixamoSkeletons() const
{
	TSharedRef< FUICommandInfo > cmd = FMixamoToolkitCommands::Get().RetargetMixamoSkeleton.ToSharedRef();

	if (CanExecuteAction_RetargetMixamoSkeletons())
	{
		return cmd->GetDescription();
	}
	else
	{
		return FText::FromString(TEXT("WARNING: Retargeting is disabled because the selected asset is not recognized as a valid Mixamo skeleton.\n"
			"Please read the documentation at https://www.unamedia.com/ue5-mixamo/docs/import-mixamo-character-in-ue5/#wrong_bones to solve the issue."));
	}
}



FText  FMixamoToolkitEditorIntegration::TooltipGetter_ExtractRootMotion() const
{
	TSharedRef< FUICommandInfo > cmd = FMixamoToolkitCommands::Get().ExtractRootMotion.ToSharedRef();

	if (CanExecuteAction_ExtractRootMotion())
	{
		return cmd->GetDescription();
	}
	else 
	{
		if (ContentBrowserSelectedAssets.Num() > 1)
		{
			return FText::FromString(TEXT("WARNING: Root motion extraction is disabled because you must select one skeleton at time."));
		}
		else
		{
			return FText::FromString(TEXT("WARNING: Root motion extraction is disabled because the selected asset was not retargeted first."));
		}
	}
}



/*
### LEGAL NOTICE ###
### WARNING ### WARNING ### WARNING ### WARNING ### WARNING ###

No changes to the code of this method are permitted, including any changes to other code that alter its intended execution.
Authors and organizations of any modifications and/or users and organizations using unauthorized modifications are considered to be in violation of the license terms.
For any need to customize this method you can contact UNAmedia.

### WARNING ### WARNING ### WARNING ### WARNING ### WARNING ###
*/
void FMixamoToolkitEditorIntegration::ExecuteChecked(TFunction<void()> AuthorizedCallback) const
{
	AuthorizedCallback();
// 	static const FText PluginFriendlyName = LOCTEXT("FMixamoToolkitEditorIntegration_PluginFriendlyName", "Mixamo Animation Retargeting 2");
// 	IPluginWardenModule::Get().CheckEntitlementForPlugin(
// 		PluginFriendlyName,
// 		TEXT("be8f43f9820c41f191084cceac460158"), // ItemID
// 		TEXT("c684998124da4e2583b314dc95403a80"), // OfferID
// 		FText::Format(LOCTEXT("FMixamoToolkitEditorIntegration_UnauthorizedFailure", "It doesn't look like you've purchased {0}.\n\nWould you like to see the store page?\n\nIf your purchase in the store page is valid, please restart the Epic Games Launcher and make sure you are signed-in to the correct account. If the issue persists, contact the Epic Marketplace support to verify your purchase."), PluginFriendlyName),
// 		IPluginWardenModule::EUnauthorizedErrorHandling::ShowMessageOpenStore,
// 		AuthorizedCallback
// 	);
}



/** Run the RetargetMixamoSkeleton action on the passed assets. */
void FMixamoToolkitEditorIntegration::ExecuteAction_RetargetMixamoSkeletons() const
{
	// Get all USkeleton objects to process.
	TArray<USkeleton *> Skeletons;
	for (const FAssetData& Asset : ContentBrowserSelectedAssets)
	{
		if (CanExecuteAction_RetargetMixamoSkeleton(Asset))
		{
			USkeleton * o = CastChecked<USkeleton> (Asset.GetAsset());
			if (o != nullptr)
			{
				Skeletons.Add(o);
			}
		}
	}

	const int32 NumOfWarningsBeforeRetargeting 
		= FMessageLog("LogMixamoToolkit").NumMessages(EMessageSeverity::Warning);

	FMixamoAnimationRetargetingModule::Get().GetMixamoSkeletonRetargeter()->RetargetToUE4Mannequin(Skeletons);

	// check if we have to open the message log window.
	if (FMessageLog("LogMixamoToolkit").NumMessages(EMessageSeverity::Warning) != NumOfWarningsBeforeRetargeting)
	{
		FMessageLog("LogMixamoToolkit").Open(EMessageSeverity::Warning);
	}
}



/** Returns if the RetargetMixamoSkeleton action can run. */
bool FMixamoToolkitEditorIntegration::CanExecuteAction_RetargetMixamoSkeleton(const FAssetData & Asset) const
{
	// Check the asset type.
	if (Asset.AssetClassPath != USkeleton::StaticClass()->GetClassPathName())
	{
		return false;
	}
	// Check the asset content.
	// NOTE: this will load the asset if needed.
	if (!FMixamoAnimationRetargetingModule::Get().GetMixamoSkeletonRetargeter()->IsMixamoSkeleton(Cast<USkeleton> (Asset.GetAsset())))
	{
		return false;
	}
	
	return true;
}



/** Returns if the RetargetMixamoSkeleton action can run on selected assets (editor will gray-out it otherwise). */
bool FMixamoToolkitEditorIntegration::CanExecuteAction_RetargetMixamoSkeletons() const
{
	// Return true if any of SelectedAssets can be processed.
	return ContentBrowserSelectedAssets.ContainsByPredicate(
		[this](const FAssetData & asset)
	{
		return CanExecuteAction_RetargetMixamoSkeleton(asset);
	}
	);
}



void FMixamoToolkitEditorIntegration::ExecuteAction_ExtractRootMotion() const
{
    // Get all USkeleton objects to process.
    USkeleton * Skeleton(nullptr);
    for (const FAssetData& Asset : ContentBrowserSelectedAssets)
    {
        if (CanExecuteAction_ExtractRootMotion(Asset))
        {
            USkeleton * o = CastChecked<USkeleton>(Asset.GetAsset());
            if (o != nullptr)
            {
                Skeleton = o;
                break;
            }
        }
    }

	const int32 NumOfWarningsBeforeRetargeting
		= FMessageLog("LogMixamoToolkit").NumMessages(EMessageSeverity::Warning);

    FMixamoAnimationRetargetingModule::Get().GetMixamoAnimationRootMotionSolver()->LaunchProcedureFlow(Skeleton);

	// check if we have to open the message log window.
	if (FMessageLog("LogMixamoToolkit").NumMessages(EMessageSeverity::Warning) != NumOfWarningsBeforeRetargeting)
	{
		FMessageLog("LogMixamoToolkit").Open(EMessageSeverity::Warning);
	}
}



bool FMixamoToolkitEditorIntegration::CanExecuteAction_ExtractRootMotion(const FAssetData & Asset) const
{
    // Check the asset type.
    if (Asset.AssetClassPath != USkeleton::StaticClass()->GetClassPathName())
    {
        return false;
    }

    USkeleton* Skeleton = Cast<USkeleton>(Asset.GetAsset());
    return FMixamoAnimationRetargetingModule::Get().GetMixamoAnimationRootMotionSolver()->CanExecuteProcedure(Skeleton);
}



bool FMixamoToolkitEditorIntegration::CanExecuteAction_ExtractRootMotion() const
{
    if (ContentBrowserSelectedAssets.Num() != 1)
        return false;

    // Return true if any of SelectedAssets can be processed.
    return ContentBrowserSelectedAssets.ContainsByPredicate(
        [this](const FAssetData & asset)
    {
        return CanExecuteAction_ExtractRootMotion(asset);
    }
    );
}



/** Called when the ContentBrowser asks for extenders on selected assets. */
TSharedRef<FExtender> FMixamoToolkitEditorIntegration::MakeContentBrowserContextMenuExtender(const TArray<FAssetData> & NewSelectedAssets)
{
	ContentBrowserSelectedAssets = NewSelectedAssets;

	TSharedRef<FExtender> Extender(new FExtender());

	// Enable the action on supported asset types, use CanExecuteAction_RetargetMixamoSkeleton() to check later
	// if the asset object can be affected.
	bool bAnySupportedAssets = false;
	for (const FAssetData& Asset: ContentBrowserSelectedAssets)
	{
		bAnySupportedAssets = bAnySupportedAssets || (Asset.AssetClassPath == USkeleton::StaticClass()->GetClassPathName());
	}

	if (bAnySupportedAssets)
	{
		// Add the actions to the extender
		Extender->AddMenuExtension(
			"GetAssetActions",
			EExtensionHook::After,
			PluginCommands,
			// To use an intermediary sub-menu: FMenuExtensionDelegate::CreateSP(this, &FMixamoToolkitEditorIntegration::AddContentBrowserContextSubMenu)
			FMenuExtensionDelegate::CreateSP(this, &FMixamoToolkitEditorIntegration::AddContentBrowserContextMenuEntries)
		);
	}

	return Extender;
}



void FMixamoToolkitEditorIntegration::AddContentBrowserContextSubMenu(class FMenuBuilder& MenuBuilder) const
{
	// Add the submenu only if we can execute some actions.
	if (!CanExecuteAction_RetargetMixamoSkeletons())
	{
		return;
	}

	MenuBuilder.AddSubMenu(
		LOCTEXT("FMixamoToolkitEditorIntegration_ContentBrowser_SubMenuLabel", "Mixamo Asset Actions"),
		LOCTEXT("FMixamoToolkitEditorIntegration_ContentBrowser_SubMenuToolTip", "Other Mixamo Asset Actions"),
		FNewMenuDelegate::CreateSP(this, &FMixamoToolkitEditorIntegration::AddContentBrowserContextMenuEntries),
		FUIAction(),
		NAME_None,	// InExtensionHook
		EUserInterfaceActionType::Button,
		false,	// bInOpenSubMenuOnClick
		FSlateIcon(FMixamoToolkitStyle::GetStyleSetName(), "ContentBrowser.AssetActions")
	);
}



void FMixamoToolkitEditorIntegration::AddContentBrowserContextMenuEntries(class FMenuBuilder& MenuBuilder) const
{
	TFunction<void()> RetargetMixamoSkeletonsFunction = [this]() { ExecuteAction_RetargetMixamoSkeletons(); };

	// Add the RetargetMixamoSkeleton action.
	TSharedRef< FUICommandInfo > cmd = FMixamoToolkitCommands::Get().RetargetMixamoSkeleton.ToSharedRef();
	MenuBuilder.AddMenuEntry(
		cmd->GetLabel(),
		TAttribute<FText>::CreateSP(this, &FMixamoToolkitEditorIntegration::TooltipGetter_RetargetMixamoSkeletons),
		cmd->GetIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &FMixamoToolkitEditorIntegration::ExecuteChecked, RetargetMixamoSkeletonsFunction),
			FCanExecuteAction::CreateSP(this, &FMixamoToolkitEditorIntegration::CanExecuteAction_RetargetMixamoSkeletons)
		)
	);

	TFunction<void()> ExtractRootMotionFunction = [this]() { ExecuteAction_ExtractRootMotion(); };

	// Add the ExtractRootMotion action.
	cmd = FMixamoToolkitCommands::Get().ExtractRootMotion.ToSharedRef();
	MenuBuilder.AddMenuEntry(
		cmd->GetLabel(),
		TAttribute<FText>::CreateSP(this, &FMixamoToolkitEditorIntegration::TooltipGetter_ExtractRootMotion),
		cmd->GetIcon(),
		FUIAction(
			FExecuteAction::CreateSP(this, &FMixamoToolkitEditorIntegration::ExecuteChecked, ExtractRootMotionFunction),
			FCanExecuteAction::CreateSP(this, &FMixamoToolkitEditorIntegration::CanExecuteAction_ExtractRootMotion)
		)
	);
}



#undef LOCTEXT_NAMESPACE
