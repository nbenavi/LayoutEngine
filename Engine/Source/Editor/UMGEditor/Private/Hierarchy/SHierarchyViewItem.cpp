// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "UMGEditorPrivatePCH.h"
#include "SHierarchyViewItem.h"

#include "UMGEditorActions.h"
#include "WidgetTemplateDragDropOp.h"

#include "PreviewScene.h"
#include "SceneViewport.h"

#include "BlueprintEditor.h"
#include "SKismetInspector.h"
#include "BlueprintEditorUtils.h"

#include "Kismet2NameValidators.h"

#include "WidgetBlueprintEditor.h"
#include "SInlineEditableTextBlock.h"
#include "Components/Widget.h"

#include "WidgetBlueprintEditorUtils.h"
#include "Components/PanelSlot.h"
#include "ScopedTransaction.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"

#define LOCTEXT_NAMESPACE "UMG"

/**
*
*/
class FHierarchyWidgetDragDropOp : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FHierarchyWidgetDragDropOp, FDecoratedDragDropOp)

		virtual ~FHierarchyWidgetDragDropOp();

	virtual void OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent) override;

	/** The slot properties for the old slot the widget was in, is used to attempt to reapply the same layout information */
	TMap<FName, FString> ExportedSlotProperties;

	/** The widget being dragged and dropped */
	FWidgetReference Widget;

	/** The original parent of the widget. */
	UWidget* WidgetParent;

	/** The widget being dragged and dropped */
	FScopedTransaction* Transaction;

	/** Constructs a new drag/drop operation */
	static TSharedRef<FHierarchyWidgetDragDropOp> New(UWidgetBlueprint* Blueprint, FWidgetReference InWidget);
};

TSharedRef<FHierarchyWidgetDragDropOp> FHierarchyWidgetDragDropOp::New(UWidgetBlueprint* Blueprint, FWidgetReference InWidget)
{
	TSharedRef<FHierarchyWidgetDragDropOp> Operation = MakeShareable(new FHierarchyWidgetDragDropOp());
	Operation->Widget = InWidget;
	Operation->DefaultHoverText = InWidget.GetTemplate()->GetLabelText();
	Operation->CurrentHoverText = InWidget.GetTemplate()->GetLabelText();
	Operation->Construct();

	FWidgetBlueprintEditorUtils::ExportPropertiesToText(InWidget.GetTemplate()->Slot, Operation->ExportedSlotProperties);

	Operation->Transaction = new FScopedTransaction(LOCTEXT("Designer_MoveWidget", "Move Widget"));

	Blueprint->WidgetTree->SetFlags(RF_Transactional);
	Blueprint->WidgetTree->Modify();

	UWidget* Widget = Operation->Widget.GetTemplate();
	Widget->Modify();

	Operation->WidgetParent = Widget->GetParent();

	if ( Operation->WidgetParent )
	{
		Operation->WidgetParent->Modify();
	}

	return Operation;
}

FHierarchyWidgetDragDropOp::~FHierarchyWidgetDragDropOp()
{
	delete Transaction;
}

void FHierarchyWidgetDragDropOp::OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent)
{
	if ( !bDropWasHandled )
	{
		Transaction->Cancel();
	}
}

//////////////////////////////////////////////////////////////////////////

TOptional<EItemDropZone> ProcessHierarchyDragDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, bool bIsDrop, TSharedPtr<FWidgetBlueprintEditor> BlueprintEditor, FWidgetReference TargetItem, TOptional<int32> Index = TOptional<int32>())
{
	UWidget* TargetTemplate = TargetItem.GetTemplate();

	if ( TargetTemplate && ( DropZone == EItemDropZone::AboveItem || DropZone == EItemDropZone::BelowItem ) )
	{
		if ( UPanelWidget* TargetParentTemplate = Cast<UPanelWidget>(TargetTemplate->GetParent()) )
		{
			int32 InsertIndex = TargetParentTemplate->GetChildIndex(TargetTemplate);
			InsertIndex += ( DropZone == EItemDropZone::AboveItem ) ? 0 : 1;
			InsertIndex = FMath::Clamp(InsertIndex, 0, TargetParentTemplate->GetChildrenCount());

			FWidgetReference TargetParentTemplateRef = BlueprintEditor->GetReferenceFromTemplate(TargetParentTemplate);
			TOptional<EItemDropZone> ParentZone = ProcessHierarchyDragDrop(DragDropEvent, EItemDropZone::OntoItem, bIsDrop, BlueprintEditor, TargetParentTemplateRef, InsertIndex);
			if ( ParentZone.IsSet() )
			{
				return DropZone;
			}
			else
			{
				DropZone = EItemDropZone::OntoItem;
			}
		}
	}
	else
	{
		DropZone = EItemDropZone::OntoItem;
	}

	UWidgetBlueprint* Blueprint = BlueprintEditor->GetWidgetBlueprintObj();

	// Is this a drag/drop op to create a new widget in the tree?
	TSharedPtr<FWidgetTemplateDragDropOp> TemplateDragDropOp = DragDropEvent.GetOperationAs<FWidgetTemplateDragDropOp>();
	if ( TemplateDragDropOp.IsValid() )
	{
		TemplateDragDropOp->ResetToDefaultToolTip();
		TemplateDragDropOp->SetCursorOverride(TOptional<EMouseCursor::Type>());

		// Are we adding to the root?
		if ( !TargetItem.IsValid() && Blueprint->WidgetTree->RootWidget == nullptr )
		{
			// TODO UMG Allow showing a preview of this.
			if ( bIsDrop )
			{
				Blueprint->WidgetTree->RootWidget = TemplateDragDropOp->Template->Create(Blueprint->WidgetTree);
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			}

			return EItemDropZone::OntoItem;
		}
		// Are we adding to a panel?
		else if ( UPanelWidget* Parent = Cast<UPanelWidget>(TargetItem.GetTemplate()) )
		{
			if (!Parent->CanAddMoreChildren())
			{
				TemplateDragDropOp->CurrentHoverText = LOCTEXT("NoAdditionalChildren", "Widget can't accept additional children.");
			}
			else
			{
				// TODO UMG Allow showing a preview of this.
				if (bIsDrop)
				{
					UWidget* Widget = TemplateDragDropOp->Template->Create(Blueprint->WidgetTree);

					UPanelSlot* NewSlot = nullptr;
					if (Index.IsSet())
					{
						NewSlot = Parent->InsertChildAt(Index.GetValue(), Widget);
					}
					else
					{
						NewSlot = Parent->AddChild(Widget);
					}
					check(NewSlot);

					FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
				}

				return EItemDropZone::OntoItem;
			}
		}
		else
		{
			TemplateDragDropOp->CurrentHoverText = LOCTEXT("CantHaveChildren", "Widget can't have children.");
		}

		TemplateDragDropOp->SetCursorOverride(EMouseCursor::SlashedCircle);
		return TOptional<EItemDropZone>();
	}

	TSharedPtr<FHierarchyWidgetDragDropOp> HierarchyDragDropOp = DragDropEvent.GetOperationAs<FHierarchyWidgetDragDropOp>();
	if ( HierarchyDragDropOp.IsValid() )
	{
		HierarchyDragDropOp->SetCursorOverride(TOptional<EMouseCursor::Type>());
		HierarchyDragDropOp->ResetToDefaultToolTip();

		// If the target item is valid we're dealing with a normal widget in the hierarchy, otherwise we should assume it's
		// the null case and we should be adding it as the root widget.
		if ( TargetItem.IsValid() )
		{
			const bool bIsDraggedObject = TargetItem.GetTemplate() == HierarchyDragDropOp->Widget.GetTemplate();
			if ( bIsDraggedObject )
			{
				HierarchyDragDropOp->SetCursorOverride(EMouseCursor::SlashedCircle);
				return TOptional<EItemDropZone>();
			}

			UPanelWidget* NewParent = Cast<UPanelWidget>(TargetItem.GetTemplate());
			if ( !NewParent )
			{
				HierarchyDragDropOp->SetCursorOverride(EMouseCursor::SlashedCircle);
				HierarchyDragDropOp->CurrentHoverText = LOCTEXT("CantHaveChildren", "Widget can't have children.");
				return TOptional<EItemDropZone>();
			}

			if ( !NewParent->CanAddMoreChildren() )
			{
				HierarchyDragDropOp->SetCursorOverride(EMouseCursor::SlashedCircle);
				HierarchyDragDropOp->CurrentHoverText = LOCTEXT("NoAdditionalChildren", "Widget can't accept additional children.");
				return TOptional<EItemDropZone>();
			}

			UWidget* TemplateWidget = HierarchyDragDropOp->Widget.GetTemplate();

			// Verify that the new location we're placing the widget is not inside of its existing children.
			bool bFoundNewParentInChildSet = false;
			Blueprint->WidgetTree->ForWidgetAndChildren(TemplateWidget, [&] (UWidget* Widget) {
				if ( NewParent == Widget )
				{
					bFoundNewParentInChildSet = true;
				}
			});

			if ( bFoundNewParentInChildSet )
			{
				HierarchyDragDropOp->SetCursorOverride(EMouseCursor::SlashedCircle);
				HierarchyDragDropOp->CurrentHoverText = LOCTEXT("CantMakeWidgetChildOfChildren", "Can't make widget a child of its children.");
				return TOptional<EItemDropZone>();
			}

			if ( bIsDrop )
			{
				if ( Index.IsSet() )
				{
					// If we're inserting at an index, and the widget we're moving is already
					// in the hierarchy before the point we're moving it to, we need to reduce the index
					// count by one, because the whole set is about to be shifted when it's removed.
					const bool bInsertInSameParent = TemplateWidget->GetParent() == NewParent;
					const bool bNeedToDropIndex = NewParent->GetChildIndex(TemplateWidget) < Index.GetValue();

					if ( bInsertInSameParent && bNeedToDropIndex )
					{
						Index = Index.GetValue() - 1;
					}
				}

				TemplateWidget->RemoveFromParent();

				NewParent->SetFlags(RF_Transactional);
				NewParent->Modify();

				UPanelSlot* NewSlot = nullptr;
				if ( Index.IsSet() )
				{
					NewSlot = NewParent->InsertChildAt(Index.GetValue(), TemplateWidget);
				}
				else
				{
					NewSlot = NewParent->AddChild(TemplateWidget);
				}
				check(NewSlot);

				// Import the old slot properties
				FWidgetBlueprintEditorUtils::ImportPropertiesFromText(NewSlot, HierarchyDragDropOp->ExportedSlotProperties);
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

				TSet<FWidgetReference> SelectedTemplates;
				SelectedTemplates.Add(BlueprintEditor->GetReferenceFromTemplate(TemplateWidget));

				BlueprintEditor->SelectWidgets(SelectedTemplates, false);
			}

			return EItemDropZone::OntoItem;
		}
		else
		{
			HierarchyDragDropOp->SetCursorOverride(EMouseCursor::SlashedCircle);
		}

		return TOptional<EItemDropZone>();
	}

	return TOptional<EItemDropZone>();
}


//////////////////////////////////////////////////////////////////////////

FHierarchyModel::FHierarchyModel()
	: bInitialized(false)
	, bIsSelected(false)
{

}

TOptional<EItemDropZone> FHierarchyModel::HandleCanAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone)
{
	return TOptional<EItemDropZone>();
}

FReply FHierarchyModel::HandleDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return FReply::Unhandled();
}

void FHierarchyModel::HandleDragEnter(const FDragDropEvent& DragDropEvent)
{

}

void FHierarchyModel::HandleDragLeave(const FDragDropEvent& DragDropEvent)
{
	TSharedPtr<FDecoratedDragDropOp> DecoratedDragDropOp = DragDropEvent.GetOperationAs<FDecoratedDragDropOp>();
	if ( DecoratedDragDropOp.IsValid() )
	{
		DecoratedDragDropOp->SetCursorOverride(TOptional<EMouseCursor::Type>());
		DecoratedDragDropOp->ResetToDefaultToolTip();
	}
}

FReply FHierarchyModel::HandleAcceptDrop(FDragDropEvent const& DragDropEvent, EItemDropZone DropZone)
{
	return FReply::Unhandled();
}

bool FHierarchyModel::OnVerifyNameTextChanged(const FText& InText, FText& OutErrorMessage)
{
	return false;
}

void FHierarchyModel::OnNameTextCommited(const FText& InText, ETextCommit::Type CommitInfo)
{

}

void FHierarchyModel::InitializeChildren()
{
	if ( !bInitialized )
	{
		bInitialized = true;
		GetChildren(Models);
	}
}

void FHierarchyModel::GatherChildren(TArray< TSharedPtr<FHierarchyModel> >& Children)
{
	InitializeChildren();

	Children.Append(Models);
}

bool FHierarchyModel::ContainsSelection()
{
	InitializeChildren();

	for ( TSharedPtr<FHierarchyModel>& Model : Models )
	{
		if ( Model->IsSelected() || Model->ContainsSelection() )
		{
			return true;
		}
	}

	return false;
}

void FHierarchyModel::RefreshSelection()
{
	InitializeChildren();

	UpdateSelection();

	for ( TSharedPtr<FHierarchyModel>& Model : Models )
	{
		Model->RefreshSelection();
	}
}

bool FHierarchyModel::IsSelected() const
{
	return bIsSelected;
}

//////////////////////////////////////////////////////////////////////////

FHierarchyRoot::FHierarchyRoot(TSharedPtr<FWidgetBlueprintEditor> InBlueprintEditor)
	: BlueprintEditor(InBlueprintEditor)
{
	RootText = FText::Format(LOCTEXT("RootWidgetFormat", "[{0}]"), FText::FromString(BlueprintEditor.Pin()->GetBlueprintObj()->GetName()));
}

FName FHierarchyRoot::GetUniqueName() const
{
	static const FName DesignerRootName(TEXT("WidgetDesignerRoot"));
	return DesignerRootName;
}

FText FHierarchyRoot::GetText() const
{
	return RootText;
}

const FSlateBrush* FHierarchyRoot::GetImage() const
{
	return nullptr;
}

FSlateFontInfo FHierarchyRoot::GetFont() const
{
	return FSlateFontInfo(FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Bold.ttf"), 10);
}

void FHierarchyRoot::GetChildren(TArray< TSharedPtr<FHierarchyModel> >& Children)
{
	TSharedPtr<FWidgetBlueprintEditor> BPEd = BlueprintEditor.Pin();
	UWidgetBlueprint* Blueprint = BPEd->GetWidgetBlueprintObj();

	if ( Blueprint->WidgetTree->RootWidget )
	{
		TSharedPtr<FHierarchyWidget> RootChild = MakeShareable(new FHierarchyWidget(BPEd->GetReferenceFromTemplate(Blueprint->WidgetTree->RootWidget), BPEd));
		Children.Add(RootChild);
	}
}

void FHierarchyRoot::OnSelection()
{
	TSharedPtr<FWidgetBlueprintEditor> BPEd = BlueprintEditor.Pin();
	if ( UWidget* Default = BPEd->GetWidgetBlueprintObj()->GeneratedClass->GetDefaultObject<UWidget>() )
	{
		TSet<UObject*> SelectedObjects;
		SelectedObjects.Add(Default);
		BPEd->SelectObjects(SelectedObjects);
	}
}

void FHierarchyRoot::UpdateSelection()
{
	TSharedPtr<FWidgetBlueprintEditor> BPEd = BlueprintEditor.Pin();
	if ( UWidget* Default = BPEd->GetWidgetBlueprintObj()->GeneratedClass->GetDefaultObject<UWidget>() )
	{
		const TSet< TWeakObjectPtr<UObject> >& SelectedObjects = BlueprintEditor.Pin()->GetSelectedObjects();
		bIsSelected = SelectedObjects.Contains(Default);
	}
	else
	{
		bIsSelected = false;
	}
}

TOptional<EItemDropZone> FHierarchyRoot::HandleCanAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone)
{
	bool bIsDrop = false;
	return ProcessHierarchyDragDrop(DragDropEvent, DropZone, bIsDrop, BlueprintEditor.Pin(), FWidgetReference());
}

FReply FHierarchyRoot::HandleAcceptDrop(FDragDropEvent const& DragDropEvent, EItemDropZone DropZone)
{
	bool bIsDrop = true;
	TOptional<EItemDropZone> Zone = ProcessHierarchyDragDrop(DragDropEvent, DropZone, bIsDrop, BlueprintEditor.Pin(), FWidgetReference());
	return Zone.IsSet() ? FReply::Handled() : FReply::Unhandled();
}

//////////////////////////////////////////////////////////////////////////

FNamedSlotModel::FNamedSlotModel(FWidgetReference InItem, FName InSlotName, TSharedPtr<FWidgetBlueprintEditor> InBlueprintEditor)
	: Item(InItem)
	, SlotName(InSlotName)
	, BlueprintEditor(InBlueprintEditor)
{
}

FName FNamedSlotModel::GetUniqueName() const
{
	UWidget* WidgetTemplate = Item.GetTemplate();
	if ( WidgetTemplate )
	{
		FString UniqueSlot = WidgetTemplate->GetName() + TEXT(".") + SlotName.ToString();
		return FName(*UniqueSlot);
	}

	return NAME_None;
}

FText FNamedSlotModel::GetText() const
{
	if ( INamedSlotInterface* NamedSlotHost = Cast<INamedSlotInterface>(Item.GetTemplate()) )
	{
		TSet<FWidgetReference> SelectedWidgets;
		if ( UWidget* SlotContent = NamedSlotHost->GetContentForSlot(SlotName) )
		{
			return FText::Format(LOCTEXT("NamedSlotTextFormat", "{0} ({1})"), FText::FromName(SlotName), FText::FromName(SlotContent->GetFName()));
		}
	}

	return FText::FromName(SlotName);
}

const FSlateBrush* FNamedSlotModel::GetImage() const
{
	return NULL;
}

FSlateFontInfo FNamedSlotModel::GetFont() const
{
	return FSlateFontInfo(FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Bold.ttf"), 10);
}

void FNamedSlotModel::GetChildren(TArray< TSharedPtr<FHierarchyModel> >& Children)
{
	TSharedPtr<FWidgetBlueprintEditor> BPEd = BlueprintEditor.Pin();
	if ( INamedSlotInterface* NamedSlotHost = Cast<INamedSlotInterface>(Item.GetTemplate()) )
	{
		TSet<FWidgetReference> SelectedWidgets;
		if ( UWidget* TemplateSlotContent = NamedSlotHost->GetContentForSlot(SlotName) )
		{
			TSharedPtr<FHierarchyWidget> RootChild = MakeShareable(new FHierarchyWidget(BPEd->GetReferenceFromTemplate(TemplateSlotContent), BPEd));
			Children.Add(RootChild);
		}
	}
}

void FNamedSlotModel::OnSelection()
{
	TSharedPtr<FWidgetBlueprintEditor> BPEd = BlueprintEditor.Pin();
	if ( INamedSlotInterface* NamedSlotHost = Cast<INamedSlotInterface>(Item.GetTemplate()) )
	{
		TSet<FWidgetReference> SelectedWidgets;
		if ( UWidget* TemplateSlotContent = NamedSlotHost->GetContentForSlot(SlotName) )
		{
			SelectedWidgets.Add(BPEd->GetReferenceFromTemplate(TemplateSlotContent));
		}

		BPEd->SelectWidgets(SelectedWidgets, true);
	}
}

void FNamedSlotModel::UpdateSelection()
{
	//bIsSelected = false;

	//const TSet<FWidgetReference>& SelectedWidgets = BlueprintEditor.Pin()->GetSelectedWidgets();

	//TSharedPtr<FWidgetBlueprintEditor> BPEd = BlueprintEditor.Pin();
	//if ( INamedSlotInterface* NamedSlotHost = Cast<INamedSlotInterface>(Item.GetTemplate()) )
	//{
	//	if ( UWidget* TemplateSlotContent = NamedSlotHost->GetContentForSlot(SlotName) )
	//	{
	//		bIsSelected = SelectedWidgets.Contains(BPEd->GetReferenceFromTemplate(TemplateSlotContent));
	//	}
	//}
}

TOptional<EItemDropZone> FNamedSlotModel::HandleCanAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone)
{
	TSharedPtr<FWidgetTemplateDragDropOp> TemplateDragDropOp = DragDropEvent.GetOperationAs<FWidgetTemplateDragDropOp>();
	if ( TemplateDragDropOp.IsValid() )
	{
		if ( INamedSlotInterface* NamedSlotHost = Cast<INamedSlotInterface>(Item.GetTemplate()) )
		{
			// Only assign content to the named slot if it is null.
			if ( NamedSlotHost->GetContentForSlot(SlotName) != nullptr )
			{
				TSharedPtr<FDecoratedDragDropOp> DecoratedDragDropOp = DragDropEvent.GetOperationAs<FDecoratedDragDropOp>();
				TemplateDragDropOp->SetCursorOverride(EMouseCursor::SlashedCircle);

				return TOptional<EItemDropZone>();
			}

			return EItemDropZone::OntoItem;
		}
	}

	return TOptional<EItemDropZone>();
}

FReply FNamedSlotModel::HandleAcceptDrop(FDragDropEvent const& DragDropEvent, EItemDropZone DropZone)
{
	UWidgetBlueprint* Blueprint = BlueprintEditor.Pin()->GetWidgetBlueprintObj();

	// Is this a drag/drop op to create a new widget in the tree?
	TSharedPtr<FWidgetTemplateDragDropOp> TemplateDragDropOp = DragDropEvent.GetOperationAs<FWidgetTemplateDragDropOp>();
	if ( TemplateDragDropOp.IsValid() )
	{
		if ( INamedSlotInterface* NamedSlotHost = Cast<INamedSlotInterface>(Item.GetTemplate()) )
		{
			// Only assign content to the named slot if it is null.
			if ( NamedSlotHost->GetContentForSlot(SlotName) == nullptr )
			{
				UWidget* Widget = TemplateDragDropOp->Template->Create(Blueprint->WidgetTree);
				NamedSlotHost->SetContentForSlot(SlotName, Widget);
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

				TSet<FWidgetReference> SelectedTemplates;
				SelectedTemplates.Add(BlueprintEditor.Pin()->GetReferenceFromTemplate(Widget));

				BlueprintEditor.Pin()->SelectWidgets(SelectedTemplates, false);
			}

			return FReply::Handled();
		}
	}

	return FReply::Unhandled();
}

FReply FNamedSlotModel::HandleDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if ( INamedSlotInterface* NamedSlotHost = Cast<INamedSlotInterface>(Item.GetTemplate()) )
	{
		// Only assign content to the named slot if it is null.
		if ( UWidget* Content = NamedSlotHost->GetContentForSlot(SlotName) )
		{
			FWidgetReference ContentRef = BlueprintEditor.Pin()->GetReferenceFromTemplate(Content);
			check(ContentRef.IsValid());

			return FReply::Handled().BeginDragDrop(FHierarchyWidgetDragDropOp::New(BlueprintEditor.Pin()->GetWidgetBlueprintObj(), ContentRef));
		}
	}

	return FReply::Unhandled();
}

//////////////////////////////////////////////////////////////////////////

FHierarchyWidget::FHierarchyWidget(FWidgetReference InItem, TSharedPtr<FWidgetBlueprintEditor> InBlueprintEditor)
	: Item(InItem)
	, BlueprintEditor(InBlueprintEditor)
{
}

FName FHierarchyWidget::GetUniqueName() const
{
	UWidget* WidgetTemplate = Item.GetTemplate();
	if ( WidgetTemplate )
	{
		return WidgetTemplate->GetFName();
	}

	return NAME_None;
}

FText FHierarchyWidget::GetText() const
{
	UWidget* WidgetTemplate = Item.GetTemplate();
	if ( WidgetTemplate )
	{
		return WidgetTemplate->GetLabelText();
	}

	return FText::GetEmpty();
}

FText FHierarchyWidget::GetImageToolTipText() const
{
	UWidget* WidgetTemplate = Item.GetTemplate();
	if ( WidgetTemplate )
	{
		UClass* WidgetClass = WidgetTemplate->GetClass();
		if ( WidgetClass->IsChildOf( UUserWidget::StaticClass() ) )
		{
			auto& Description = Cast<UWidgetBlueprint>( WidgetClass->ClassGeneratedBy )->BlueprintDescription;
			if ( Description.Len() > 0 )
			{
				return FText::FromString( Description );
			}
		}
		
		return WidgetClass->GetToolTipText();
	}
	
	return FText::GetEmpty();
}

FText FHierarchyWidget::GetLabelToolTipText() const
{
	// If the user has provided a name, give a tooltip with the widget type for easy reference
	UWidget* WidgetTemplate = Item.GetTemplate();
	if ( WidgetTemplate && !WidgetTemplate->IsGeneratedName() )
	{
		return FText::FromString(TEXT( "[" ) + WidgetTemplate->GetClass()->GetDisplayNameText().ToString() + TEXT( "]" ) );
	}

	return FText::GetEmpty();
}

const FSlateBrush* FHierarchyWidget::GetImage() const
{
	return Item.GetTemplate()->GetEditorIcon();
}

FSlateFontInfo FHierarchyWidget::GetFont() const
{
	UWidget* WidgetTemplate = Item.GetTemplate();
	if ( WidgetTemplate )
	{
		if ( !WidgetTemplate->IsGeneratedName() && WidgetTemplate->bIsVariable )
		{
			// TODO UMG Hacky move into style area
			return FSlateFontInfo(FPaths::EngineContentDir() / TEXT("Slate/Fonts/Roboto-Bold.ttf"), 10);
		}
	}

	static FName NormalFont("NormalFont");
	return FCoreStyle::Get().GetFontStyle(NormalFont);
}

TOptional<EItemDropZone> FHierarchyWidget::HandleCanAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone)
{
	bool bIsDrop = false;
	return ProcessHierarchyDragDrop(DragDropEvent, DropZone, bIsDrop, BlueprintEditor.Pin(), Item);
}

FReply FHierarchyWidget::HandleDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	const bool bIsRoot = Item.GetTemplate()->GetParent() == nullptr;

	if ( !bIsRoot )
	{
		return FReply::Handled().BeginDragDrop(FHierarchyWidgetDragDropOp::New(BlueprintEditor.Pin()->GetWidgetBlueprintObj(), Item));
	}

	return FReply::Unhandled();
}

void FHierarchyWidget::HandleDragLeave(const FDragDropEvent& DragDropEvent)
{
	TSharedPtr<FDecoratedDragDropOp> DecoratedDragDropOp = DragDropEvent.GetOperationAs<FDecoratedDragDropOp>();
	if ( DecoratedDragDropOp.IsValid() )
	{
		DecoratedDragDropOp->SetCursorOverride(TOptional<EMouseCursor::Type>());
		DecoratedDragDropOp->ResetToDefaultToolTip();
	}
}

FReply FHierarchyWidget::HandleAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone)
{
	bool bIsDrop = true;
	TOptional<EItemDropZone> Zone = ProcessHierarchyDragDrop(DragDropEvent, DropZone, bIsDrop, BlueprintEditor.Pin(), Item);
	return Zone.IsSet() ? FReply::Handled() : FReply::Unhandled();
}

bool FHierarchyWidget::OnVerifyNameTextChanged(const FText& InText, FText& OutErrorMessage)
{
	return FWidgetBlueprintEditorUtils::VerifyWidgetRename(BlueprintEditor.Pin().ToSharedRef(), Item, InText, OutErrorMessage);
}

void FHierarchyWidget::OnNameTextCommited(const FText& InText, ETextCommit::Type CommitInfo)
{
	FWidgetBlueprintEditorUtils::RenameWidget(BlueprintEditor.Pin().ToSharedRef(), Item.GetTemplate()->GetFName(), FName(*InText.ToString()));
}

void FHierarchyWidget::GetChildren(TArray< TSharedPtr<FHierarchyModel> >& Children)
{
	TSharedPtr<FWidgetBlueprintEditor> BPEd = BlueprintEditor.Pin();

	// Check for named slots
	if ( INamedSlotInterface* NamedSlotHost = Cast<INamedSlotInterface>(Item.GetTemplate()) )
	{
		TArray<FName> SlotNames;
		NamedSlotHost->GetSlotNames(SlotNames);

		for ( FName& SlotName : SlotNames )
		{
			TSharedPtr<FNamedSlotModel> ChildItem = MakeShareable(new FNamedSlotModel(Item, SlotName, BPEd));
			Children.Add(ChildItem);
		}
	}
	
	// Check if it's a panel widget that can support children
	if ( UPanelWidget* PanelWidget = Cast<UPanelWidget>(Item.GetTemplate()) )
	{
		for ( int32 i = 0; i < PanelWidget->GetChildrenCount(); i++ )
		{
			UWidget* Child = PanelWidget->GetChildAt(i);
			if ( Child )
			{
				TSharedPtr<FHierarchyWidget> ChildItem = MakeShareable(new FHierarchyWidget(BPEd->GetReferenceFromTemplate(Child), BPEd));
				Children.Add(ChildItem);
			}
		}
	}
}

void FHierarchyWidget::OnSelection()
{
	TSet<FWidgetReference> SelectedWidgets;
	SelectedWidgets.Add(Item);

	BlueprintEditor.Pin()->SelectWidgets(SelectedWidgets, true);
}

void FHierarchyWidget::OnMouseEnter()
{
	BlueprintEditor.Pin()->SetHoveredWidget(Item);
}

void FHierarchyWidget::OnMouseLeave()
{
	BlueprintEditor.Pin()->ClearHoveredWidget();
}

bool FHierarchyWidget::IsHovered() const
{
	return BlueprintEditor.Pin()->GetHoveredWidget() == Item;
}

void FHierarchyWidget::UpdateSelection()
{
	const TSet<FWidgetReference>& SelectedWidgets = BlueprintEditor.Pin()->GetSelectedWidgets();
	bIsSelected = SelectedWidgets.Contains(Item);
}

bool FHierarchyWidget::CanRename() const
{
	return true;
}

void FHierarchyWidget::BeginRename()
{
	RenameEvent.ExecuteIfBound();
}

//////////////////////////////////////////////////////////////////////////

void SHierarchyViewItem::Construct(const FArguments& InArgs, const TSharedRef< STableViewBase >& InOwnerTableView, TSharedPtr<FHierarchyModel> InModel)
{
	Model = InModel;
	Model->RenameEvent.BindSP(this, &SHierarchyViewItem::BeginRename);

	STableRow< TSharedPtr<FHierarchyModel> >::Construct(
		STableRow< TSharedPtr<FHierarchyModel> >::FArguments()
		.OnCanAcceptDrop(this, &SHierarchyViewItem::HandleCanAcceptDrop)
		.OnAcceptDrop(this, &SHierarchyViewItem::HandleAcceptDrop)
		.OnDragDetected(this, &SHierarchyViewItem::HandleDragDetected)
		.OnDragEnter(this, &SHierarchyViewItem::HandleDragEnter)
		.OnDragLeave(this, &SHierarchyViewItem::HandleDragLeave)
		.Padding(0.0f)
		.Content()
		[
			SNew(SHorizontalBox)
			
			// Widget icon
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SImage)
				.ColorAndOpacity(FLinearColor(1,1,1,0.5))
				.Image(Model->GetImage())
				.ToolTipText(Model->GetImageToolTipText())
			]

			// Name of the widget
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(2, 0, 0, 0)
			.VAlign(VAlign_Center)
			[
				SAssignNew(EditBox, SInlineEditableTextBlock)
				.Font(this, &SHierarchyViewItem::GetItemFont)
				.Text(this, &SHierarchyViewItem::GetItemText)
				.ToolTipText(Model->GetLabelToolTipText())
				.HighlightText(InArgs._HighlightText)
				.OnVerifyTextChanged(this, &SHierarchyViewItem::OnVerifyNameTextChanged)
				.OnTextCommitted(this, &SHierarchyViewItem::OnNameTextCommited)
				.IsSelected(this, &SHierarchyViewItem::IsSelectedExclusively)
			]

			// Visibility icon
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ContentPadding(FMargin(3, 1))
				.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
				.ForegroundColor(FCoreStyle::Get().GetSlateColor("Foreground"))
				.OnClicked(this, &SHierarchyViewItem::OnToggleVisibility)
				.Visibility(Model->CanControlVisibility() ? EVisibility::Visible : EVisibility::Hidden)
				.ToolTipText(LOCTEXT("WidgetVisibilityButtonToolTip", "Toggle Widget's Editor Visibility"))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
					.Text(this, &SHierarchyViewItem::GetVisibilityBrushForWidget)
				]
			]
		],
		InOwnerTableView);
}

SHierarchyViewItem::~SHierarchyViewItem()
{
	Model->RenameEvent.Unbind();
}

void SHierarchyViewItem::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	STableRow< TSharedPtr<FHierarchyModel> >::OnMouseEnter(MyGeometry, MouseEvent);

	Model->OnMouseEnter();
}

void SHierarchyViewItem::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	STableRow< TSharedPtr<FHierarchyModel> >::OnMouseLeave(MouseEvent);

	Model->OnMouseLeave();
}

bool SHierarchyViewItem::OnVerifyNameTextChanged(const FText& InText, FText& OutErrorMessage)
{
	return Model->OnVerifyNameTextChanged(InText, OutErrorMessage);
}

void SHierarchyViewItem::OnNameTextCommited(const FText& InText, ETextCommit::Type CommitInfo)
{
	// The model can return nice names "Border_53" becomes [Border] in some cases
	// This check makes sure we don't rename the object internally to that nice name.
	// Most common case would be the user enters edit mode by accident then just moves focus away.
	if (Model->GetText().EqualToCaseIgnored(InText))
	{
		return;
	}
	Model->OnNameTextCommited(InText, CommitInfo);
}

bool SHierarchyViewItem::CanRename() const
{
	return Model->CanRename();
}

void SHierarchyViewItem::BeginRename()
{
	TSharedPtr<SInlineEditableTextBlock> SafeEditBox = EditBox.Pin();
	if ( SafeEditBox.IsValid() )
	{
		SafeEditBox->EnterEditingMode();
	}
}

FSlateFontInfo SHierarchyViewItem::GetItemFont() const
{
	return Model->GetFont();
}

FText SHierarchyViewItem::GetItemText() const
{
	return Model->GetText();
}

bool SHierarchyViewItem::IsHovered() const
{
	return bIsHovered || Model->IsHovered();
}

void SHierarchyViewItem::HandleDragEnter(FDragDropEvent const& DragDropEvent)
{
	Model->HandleDragEnter(DragDropEvent);
}

void SHierarchyViewItem::HandleDragLeave(const FDragDropEvent& DragDropEvent)
{
	Model->HandleDragLeave(DragDropEvent);
}

TOptional<EItemDropZone> SHierarchyViewItem::HandleCanAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TSharedPtr<FHierarchyModel> TargetItem)
{
	return Model->HandleCanAcceptDrop(DragDropEvent, DropZone);
}

FReply SHierarchyViewItem::HandleDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	return Model->HandleDragDetected(MyGeometry, MouseEvent);
}

FReply SHierarchyViewItem::HandleAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TSharedPtr<FHierarchyModel> TargetItem)
{
	return Model->HandleAcceptDrop(DragDropEvent, DropZone);
}

FReply SHierarchyViewItem::OnToggleVisibility()
{
	Model->SetIsVisible(!Model->IsVisible());

	return FReply::Handled();
}

FText SHierarchyViewItem::GetVisibilityBrushForWidget() const
{
	return Model->IsVisible() ?
		FText::FromString(FString(TEXT("\xf06e")) /*fa-eye*/) :
		FText::FromString(FString(TEXT("\xf070")) /*fa-eye-slash*/);
}

#undef LOCTEXT_NAMESPACE
