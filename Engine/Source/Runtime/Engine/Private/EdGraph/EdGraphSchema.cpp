// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "EnginePrivate.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "BlueprintUtilities.h"
#if WITH_EDITOR
#include "Editor/UnrealEd/Public/Kismet2/BlueprintEditorUtils.h"
#include "SlateBasics.h"
#include "ScopedTransaction.h"
#include "Editor/UnrealEd/Public/Kismet2/Kismet2NameValidators.h"
#include "Editor/UnrealEd/Public/EditorCategoryUtils.h"
#endif

#define LOCTEXT_NAMESPACE "EdGraph"

/////////////////////////////////////////////////////
// FGraphActionListBuilderBase

void FGraphActionListBuilderBase::AddAction( const TSharedPtr<FEdGraphSchemaAction>& NewAction, FString const& Category/* = TEXT("") */ )
{
	Entries.Add( ActionGroup( NewAction, Category ) );
}

void FGraphActionListBuilderBase::AddActionList( const TArray<TSharedPtr<FEdGraphSchemaAction> >& NewActions, FString const& Category/* = TEXT("") */ )
{
	Entries.Add( ActionGroup( NewActions, Category ) );
}

void FGraphActionListBuilderBase::Append( FGraphActionListBuilderBase& Other )
{
	Entries.Append( Other.Entries );
}

int32 FGraphActionListBuilderBase::GetNumActions() const
{
	return Entries.Num();
}

FGraphActionListBuilderBase::ActionGroup& FGraphActionListBuilderBase::GetAction( int32 Index )
{
	return Entries[Index];
}

void FGraphActionListBuilderBase::Empty()
{
	Entries.Empty();
}

/////////////////////////////////////////////////////
// FGraphActionListBuilderBase::GraphAction

FGraphActionListBuilderBase::ActionGroup::ActionGroup( TSharedPtr<FEdGraphSchemaAction> InAction, FString const& CategoryPrefix/* = TEXT("") */ )
	: RootCategory(CategoryPrefix)
{
	Actions.Add( InAction );
}

FGraphActionListBuilderBase::ActionGroup::ActionGroup( const TArray< TSharedPtr<FEdGraphSchemaAction> >& InActions, FString const& CategoryPrefix/* = TEXT("") */ )
	: RootCategory(CategoryPrefix)
{
	Actions = InActions;
}

void FGraphActionListBuilderBase::ActionGroup::GetCategoryChain(TArray<FString>& HierarchyOut) const
{
#if WITH_EDITOR
	static FString const CategoryDelim("|");
	FEditorCategoryUtils::GetCategoryDisplayString(RootCategory).ParseIntoArray(HierarchyOut, *CategoryDelim, true);

	if (Actions.Num() > 0)
	{
		TArray<FString> SubCategoryChain;

		FString SubCategory = FEditorCategoryUtils::GetCategoryDisplayString(Actions[0]->Category);
		SubCategory.ParseIntoArray(SubCategoryChain, *CategoryDelim, true);

		HierarchyOut.Append(SubCategoryChain);
	}

	for (FString& Category : HierarchyOut)
	{
		Category.Trim();
	}
#endif
}

void FGraphActionListBuilderBase::ActionGroup::PerformAction( class UEdGraph* ParentGraph, TArray<UEdGraphPin*>& FromPins, const FVector2D Location )
{
	for ( int32 ActionIndex = 0; ActionIndex < Actions.Num(); ActionIndex++ )
	{
		TSharedPtr<FEdGraphSchemaAction> CurrentAction = Actions[ActionIndex];
		if ( CurrentAction.IsValid() )
		{
			CurrentAction->PerformAction( ParentGraph, FromPins, Location );
		}
	}
}

/////////////////////////////////////////////////////
// FCategorizedGraphActionListBuilder

static FString ConcatCategories(FString const& RootCategory, FString const& SubCategory)
{
	FString ConcatedCategory = RootCategory;
	if (!SubCategory.IsEmpty() && !RootCategory.IsEmpty())
	{
		ConcatedCategory += TEXT("|");
	}
	ConcatedCategory += SubCategory;

	return ConcatedCategory;
}

FCategorizedGraphActionListBuilder::FCategorizedGraphActionListBuilder(FString const& CategoryIn/* = TEXT("")*/)
	: Category(CategoryIn)
{
}

void FCategorizedGraphActionListBuilder::AddAction(TSharedPtr<FEdGraphSchemaAction> const& NewAction, FString const& CategoryIn/* = TEXT("")*/)
{
	FGraphActionListBuilderBase::AddAction(NewAction, ConcatCategories(Category, CategoryIn));
}

void FCategorizedGraphActionListBuilder::AddActionList(TArray<TSharedPtr<FEdGraphSchemaAction> > const& NewActions, FString const& CategoryIn/* = TEXT("")*/)
{
	FGraphActionListBuilderBase::AddActionList(NewActions, ConcatCategories(Category, CategoryIn));
}

/////////////////////////////////////////////////////
// FGraphContextMenuBuilder

FGraphContextMenuBuilder::FGraphContextMenuBuilder(const UEdGraph* InGraph) 
	: CurrentGraph(InGraph)
{
	OwnerOfTemporaries =  NewObject<UEdGraph>((UObject*)GetTransientPackage());
}

/////////////////////////////////////////////////////
// FEdGraphSchemaAction_NewNode

#define SNAP_GRID (16) // @todo ensure this is the same as SNodePanel::GetSnapGridSize()

namespace 
{
	// Maximum distance a drag can be off a node edge to require 'push off' from node
	const int32 NodeDistance = 60;
}

UEdGraphNode* FEdGraphSchemaAction_NewNode::CreateNode(class UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, class UEdGraphNode* NodeTemplate)
{
	// Duplicate template node to create new node
	UEdGraphNode* ResultNode = NULL;

#if WITH_EDITOR
	ResultNode = DuplicateObject<UEdGraphNode>(NodeTemplate, ParentGraph);
	ResultNode->SetFlags(RF_Transactional);

	ParentGraph->AddNode(ResultNode, true);

	ResultNode->CreateNewGuid();
	ResultNode->PostPlacedNewNode();
	ResultNode->AllocateDefaultPins();
	ResultNode->AutowireNewNode(FromPin);

	// For input pins, new node will generally overlap node being dragged off
	// Work out if we want to visually push away from connected node
	int32 XLocation = Location.X;
	if (FromPin && FromPin->Direction == EGPD_Input)
	{
		UEdGraphNode* PinNode = FromPin->GetOwningNode();
		const float XDelta = FMath::Abs(PinNode->NodePosX - Location.X);

		if (XDelta < NodeDistance)
		{
			// Set location to edge of current node minus the max move distance
			// to force node to push off from connect node enough to give selection handle
			XLocation = PinNode->NodePosX - NodeDistance;
		}
	}

	ResultNode->NodePosX = XLocation;
	ResultNode->NodePosY = Location.Y;
	ResultNode->SnapToGrid(SNAP_GRID);
#endif // WITH_EDITOR

	return ResultNode;
}

UEdGraphNode* FEdGraphSchemaAction_NewNode::PerformAction(class UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode/* = true*/)
{
	UEdGraphNode* ResultNode = NULL;

#if WITH_EDITOR
	// If there is a template, we actually use it
	if (NodeTemplate != NULL)
	{
		const FScopedTransaction Transaction(LOCTEXT("AddNode", "Add Node"));
		ParentGraph->Modify();
		if (FromPin)
		{
			FromPin->Modify();
		}

		ResultNode = CreateNode(ParentGraph, FromPin, Location, NodeTemplate);
	}
#endif // WITH_EDITOR

	return ResultNode;
}

UEdGraphNode* FEdGraphSchemaAction_NewNode::PerformAction(class UEdGraph* ParentGraph, TArray<UEdGraphPin*>& FromPins, const FVector2D Location, bool bSelectNewNode/* = true*/) 
{
	UEdGraphNode* ResultNode = NULL;

#if WITH_EDITOR
	if (FromPins.Num() > 0)
	{
		ResultNode = PerformAction(ParentGraph, FromPins[0], Location, bSelectNewNode);

		// Try autowiring the rest of the pins
		for (int32 Index = 1; Index < FromPins.Num(); ++Index)
		{
			ResultNode->AutowireNewNode(FromPins[Index]);
		}
	}
	else
	{
		ResultNode = PerformAction(ParentGraph, NULL, Location, bSelectNewNode);
	}
#endif // WITH_EDITOR

	return ResultNode;
}

void FEdGraphSchemaAction_NewNode::AddReferencedObjects( FReferenceCollector& Collector )
{
	FEdGraphSchemaAction::AddReferencedObjects( Collector );

	// These don't get saved to disk, but we want to make sure the objects don't get GC'd while the action array is around
	Collector.AddReferencedObject( NodeTemplate );
}

/////////////////////////////////////////////////////
// UEdGraphSchema

UEdGraphSchema::UEdGraphSchema(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool UEdGraphSchema::TryCreateConnection(UEdGraphPin* PinA, UEdGraphPin* PinB) const
{
	const FPinConnectionResponse Response = CanCreateConnection(PinA, PinB);
	bool bModified = false;

	switch (Response.Response)
	{
	case CONNECT_RESPONSE_MAKE:
		PinA->Modify();
		PinB->Modify();
		PinA->MakeLinkTo(PinB);
		bModified = true;
		break;

	case CONNECT_RESPONSE_BREAK_OTHERS_A:
		PinA->Modify();
		PinB->Modify();
		PinA->BreakAllPinLinks();
		PinA->MakeLinkTo(PinB);
		bModified = true;
		break;

	case CONNECT_RESPONSE_BREAK_OTHERS_B:
		PinA->Modify();
		PinB->Modify();
		PinB->BreakAllPinLinks();
		PinA->MakeLinkTo(PinB);
		bModified = true;
		break;

	case CONNECT_RESPONSE_BREAK_OTHERS_AB:
		PinA->Modify();
		PinB->Modify();
		PinA->BreakAllPinLinks();
		PinB->BreakAllPinLinks();
		PinA->MakeLinkTo(PinB);
		bModified = true;
		break;

	case CONNECT_RESPONSE_MAKE_WITH_CONVERSION_NODE:
		bModified = CreateAutomaticConversionNodeAndConnections(PinA, PinB);
		break;
		break;

	case CONNECT_RESPONSE_DISALLOW:
	default:
		break;
	}

#if WITH_EDITOR
	if (bModified)
	{
		PinA->GetOwningNode()->PinConnectionListChanged(PinA);
		PinB->GetOwningNode()->PinConnectionListChanged(PinB);
	}
#endif	//#if WITH_EDITOR

	return bModified;
}

bool UEdGraphSchema::CreateAutomaticConversionNodeAndConnections(UEdGraphPin* PinA, UEdGraphPin* PinB) const
{
	return false;
}

void UEdGraphSchema::TrySetDefaultValue(UEdGraphPin& Pin, const FString& NewDefaultValue) const
{
	Pin.DefaultValue = NewDefaultValue;

#if WITH_EDITOR
	UEdGraphNode* Node = Pin.GetOwningNode();
	check(Node);
	Node->PinDefaultValueChanged(&Pin);
#endif	//#if WITH_EDITOR
}

void UEdGraphSchema::TrySetDefaultObject(UEdGraphPin& Pin, UObject* NewDefaultObject) const
{
	Pin.DefaultObject = NewDefaultObject;

#if WITH_EDITOR
	UEdGraphNode* Node = Pin.GetOwningNode();
	check(Node);
	Node->PinDefaultValueChanged(&Pin);
#endif	//#if WITH_EDITOR
}

void UEdGraphSchema::TrySetDefaultText(UEdGraphPin& InPin, const FText& InNewDefaultText) const
{
	if(InNewDefaultText.IsEmpty())
	{
		InPin.DefaultTextValue = InNewDefaultText;
	}
	else
	{
#if WITH_EDITOR
		if(InNewDefaultText.IsCultureInvariant())
		{
			InPin.DefaultTextValue = InNewDefaultText;
		}
		else
		{
			InPin.DefaultTextValue = FText::ChangeKey(TEXT(""), InPin.GetOwningNode()->NodeGuid.ToString() + TEXT("_") + InPin.PinName, InNewDefaultText);
		}
#endif
	}

#if WITH_EDITOR
	UEdGraphNode* Node = InPin.GetOwningNode();
	check(Node);
	Node->PinDefaultValueChanged(&InPin);
#endif	//#if WITH_EDITOR
}

void UEdGraphSchema::BreakNodeLinks(UEdGraphNode& TargetNode) const
{
#if WITH_EDITOR
	for (TArray<UEdGraphPin*>::TIterator PinIt(TargetNode.Pins); PinIt; ++PinIt)
	{
		BreakPinLinks(*(*PinIt), false);
	}
	TargetNode.NodeConnectionListChanged();
#endif	//#if WITH_EDITOR
}

bool UEdGraphSchema::SetNodeMetaData(UEdGraphNode* Node, FName const& KeyValue)
{
	if (UPackage* Package = Node->GetOutermost())
	{
		if (UMetaData* MetaData = Package->GetMetaData())
		{
			MetaData->SetValue(Node, KeyValue, TEXT("true"));
			return true;
		}
	}
	return false;
}

void UEdGraphSchema::BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotifcation) const
{
#if WITH_EDITOR
	// Copy the old pin links
	TArray<class UEdGraphPin*> OldLinkedTo(TargetPin.LinkedTo);
#endif

	TargetPin.BreakAllPinLinks();

#if WITH_EDITOR
	TSet<UEdGraphNode*> NodeList;
	// Notify this node
	TargetPin.GetOwningNode()->PinConnectionListChanged(&TargetPin);
	NodeList.Add(TargetPin.GetOwningNode());

	// As well as all other nodes that were connected
	for (TArray<UEdGraphPin*>::TIterator PinIt(OldLinkedTo); PinIt; ++PinIt)
	{
		UEdGraphPin* OtherPin = *PinIt;
		UEdGraphNode* OtherNode = OtherPin->GetOwningNode();
		OtherNode->PinConnectionListChanged(OtherPin);
		NodeList.Add(OtherNode);
	}

	if (bSendsNodeNotifcation)
	{
		// Send all nodes that received a new pin connection a notification
		for (auto It = NodeList.CreateConstIterator(); It; ++It)
		{
			UEdGraphNode* Node = (*It);
			Node->NodeConnectionListChanged();
		}
	}
#endif	//#if WITH_EDITOR
}

void UEdGraphSchema::BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin)
{
	SourcePin->BreakLinkTo(TargetPin);

#if WITH_EDITOR
	TargetPin->GetOwningNode()->PinConnectionListChanged(TargetPin);
	SourcePin->GetOwningNode()->PinConnectionListChanged(SourcePin);

	TargetPin->GetOwningNode()->NodeConnectionListChanged();
	SourcePin->GetOwningNode()->NodeConnectionListChanged();
#endif	//#if WITH_EDITOR
}

FPinConnectionResponse UEdGraphSchema::MovePinLinks(UEdGraphPin& MoveFromPin, UEdGraphPin& MoveToPin, bool bIsIntermediateMove) const
{
#if WITH_EDITOR
	ensureMsg(bIsIntermediateMove || !MoveToPin.GetOwningNode()->GetGraph()->HasAnyFlags(RF_Transient),
		TEXT("When moving to an Intermediate pin, use FKismetCompilerContext::MovePinLinksToIntermediate() instead of UEdGraphSchema::MovePinLinks()"));
#endif // #if WITH_EDITOR

	FPinConnectionResponse FinalResponse = FPinConnectionResponse(CONNECT_RESPONSE_MAKE, TEXT(""));
	// First copy the current set of links
	TArray<UEdGraphPin*> CurrentLinks = MoveFromPin.LinkedTo;
	// Then break all links at pin we are moving from
	MoveFromPin.BreakAllPinLinks();
	// Try and make each new connection
	for (int32 i=0; i<CurrentLinks.Num(); i++)
	{
		UEdGraphPin* NewLink = CurrentLinks[i];
		FPinConnectionResponse Response = CanCreateConnection(&MoveToPin, NewLink);
		if(Response.CanSafeConnect())
		{
			MoveToPin.MakeLinkTo(NewLink);
		}
		else
		{
			FinalResponse = Response;
		}
	}
	// Move over the default values
	MoveToPin.DefaultValue = MoveFromPin.DefaultValue;
	MoveToPin.DefaultObject = MoveFromPin.DefaultObject;
	MoveToPin.DefaultTextValue = MoveFromPin.DefaultTextValue;
	return FinalResponse;
}

FPinConnectionResponse UEdGraphSchema::CopyPinLinks(UEdGraphPin& CopyFromPin, UEdGraphPin& CopyToPin, bool bIsIntermediateCopy) const
{
#if WITH_EDITOR
	ensureMsg(bIsIntermediateCopy || !CopyToPin.GetOwningNode()->GetGraph()->HasAnyFlags(RF_Transient),
		TEXT("When copying to an Intermediate pin, use FKismetCompilerContext::CopyPinLinksToIntermediate() instead of UEdGraphSchema::CopyPinLinks()"));
#endif // #if WITH_EDITOR

	FPinConnectionResponse FinalResponse = FPinConnectionResponse(CONNECT_RESPONSE_MAKE, TEXT(""));
	for (int32 i=0; i<CopyFromPin.LinkedTo.Num(); i++)
	{
		UEdGraphPin* NewLink = CopyFromPin.LinkedTo[i];
		FPinConnectionResponse Response = CanCreateConnection(&CopyToPin, NewLink);
		if (Response.CanSafeConnect())
		{
			CopyToPin.MakeLinkTo(NewLink);
		}
		else
		{
			FinalResponse = Response;
		}
	}

	CopyToPin.DefaultValue = CopyFromPin.DefaultValue;
	CopyToPin.DefaultObject = CopyFromPin.DefaultObject;
	CopyToPin.DefaultTextValue = CopyFromPin.DefaultTextValue;
	return FinalResponse;
}

FText UEdGraphSchema::GetPinDisplayName(const UEdGraphPin* Pin) const
{
	FText ResultPinName;
	check(Pin != NULL);
	if (Pin->PinFriendlyName.IsEmpty())
	{
		ResultPinName = FText::FromString(Pin->PinName);
	}
	else
	{
		ResultPinName = Pin->PinFriendlyName;
		bool bShowNodesAndPinsUnlocalized = false;
		GConfig->GetBool( TEXT("Internationalization"), TEXT("ShowNodesAndPinsUnlocalized"), bShowNodesAndPinsUnlocalized, GEditorSettingsIni );
		if (bShowNodesAndPinsUnlocalized)
		{
			ResultPinName = FText::FromString(ResultPinName.BuildSourceString());
		}
	}
	return ResultPinName;
}

void UEdGraphSchema::ConstructBasicPinTooltip(UEdGraphPin const& Pin, FText const& PinDescription, FString& TooltipOut) const
{
	TooltipOut = PinDescription.ToString();
}

void UEdGraphSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
#if WITH_EDITOR
	// Run thru all nodes and add any menu items they want to add
	for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
	{
		UClass* Class = *ClassIt;
		if (Class->IsChildOf(UEdGraphNode::StaticClass()) && !Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
		{
			const UEdGraphNode* ClassCDO = Class->GetDefaultObject<UEdGraphNode>();

			if (ClassCDO->CanCreateUnderSpecifiedSchema(this))
			{
				ClassCDO->GetMenuEntries(ContextMenuBuilder);
			}
		}
	}
#endif
}

void UEdGraphSchema::ReconstructNode(UEdGraphNode& TargetNode, bool bIsBatchRequest/*=false*/) const
{
#if WITH_EDITOR
	TargetNode.ReconstructNode();
#endif	//#if WITH_EDITOR
}

void UEdGraphSchema::GetGraphDisplayInformation(const UEdGraph& Graph, /*out*/ FGraphDisplayInfo& DisplayInfo) const
{
	DisplayInfo.PlainName = FText::FromString( Graph.GetName() );
	DisplayInfo.DisplayName = DisplayInfo.PlainName;
}

void UEdGraphSchema::GetContextMenuActions(const UEdGraph* CurrentGraph, const UEdGraphNode* InGraphNode, const UEdGraphPin* InGraphPin, class FMenuBuilder* MenuBuilder, bool bIsDebugging) const
{
#if WITH_EDITOR
	FGraphNodeContextMenuBuilder Context(CurrentGraph, InGraphNode, InGraphPin, MenuBuilder, bIsDebugging);

	if (Context.Node)
	{
		Context.Node->GetContextMenuActions(Context);
	}

	if (InGraphNode)
	{
		// Helper to do the node comment editing
		struct Local
		{
			// Called by the EditableText widget to get the current comment for the node
			static FString GetNodeComment(TWeakObjectPtr<UEdGraphNode> NodeWeakPtr)
			{
				if (UEdGraphNode* SelectedNode = NodeWeakPtr.Get())
				{
					return SelectedNode->NodeComment;
				}
				return FString();
			}

			// Called by the EditableText widget when the user types a new comment for the selected node
			static void OnNodeCommentTextCommitted(const FText& NewText, ETextCommit::Type /*CommitInfo*/, TWeakObjectPtr<UEdGraphNode> NodeWeakPtr)
			{
				// Apply the change to the selected actor
				UEdGraphNode* SelectedNode = NodeWeakPtr.Get();
				FString NewString = NewText.ToString();
				if (SelectedNode && !SelectedNode->NodeComment.Equals( NewString, ESearchCase::CaseSensitive ))
				{
					// send property changed events
					const FScopedTransaction Transaction( LOCTEXT("EditNodeComment", "Change Node Comment") );
					SelectedNode->Modify();
					UProperty* NodeCommentProperty = FindField<UProperty>(SelectedNode->GetClass(), "NodeComment");
					if(NodeCommentProperty != NULL)
					{
						SelectedNode->PreEditChange(NodeCommentProperty);

						SelectedNode->NodeComment = NewString;

						FPropertyChangedEvent NodeCommentPropertyChangedEvent(NodeCommentProperty);
						SelectedNode->PostEditChangeProperty(NodeCommentPropertyChangedEvent);
					}
				}

				FSlateApplication::Get().DismissAllMenus();
			}
		};

		if( !InGraphPin )
		{
			int32 SelectionCount = GetNodeSelectionCount(CurrentGraph);
		
			if( SelectionCount == 1 )
			{
				// Node comment area
				TSharedRef<SHorizontalBox> NodeCommentBox = SNew(SHorizontalBox);

				MenuBuilder->BeginSection("GraphNodeComment", LOCTEXT("NodeCommentMenuHeader", "Node Comment"));
				{
					MenuBuilder->AddWidget(NodeCommentBox, FText::GetEmpty() );
				}
				TWeakObjectPtr<UEdGraphNode> SelectedNodeWeakPtr = InGraphNode;

				FText NodeCommentText;
				if ( UEdGraphNode* SelectedNode = SelectedNodeWeakPtr.Get() )
				{
					NodeCommentText = FText::FromString( SelectedNode->NodeComment );
				}

			const FSlateBrush* NodeIcon = FCoreStyle::Get().GetDefaultBrush();//@TODO: FActorIconFinder::FindIconForActor(SelectedActors(0).Get());


				// Comment label
				NodeCommentBox->AddSlot()
				.VAlign(VAlign_Center)
				.FillWidth( 1.0f )
				[
					SNew(SMultiLineEditableTextBox)
					.Text( NodeCommentText )
					.ToolTipText(LOCTEXT("NodeComment_ToolTip", "Comment for this node"))
					.OnTextCommitted_Static(&Local::OnNodeCommentTextCommitted, SelectedNodeWeakPtr)
					.SelectAllTextWhenFocused( true )
					.RevertTextOnEscape( true )
					.ModiferKeyForNewLine( EModifierKey::Control )
				];
				MenuBuilder->EndSection();
			}
			else if( SelectionCount > 1 )
			{
				struct SCommentUtility
				{
					static void CreateComment(const UEdGraphSchema* Schema, UEdGraph* Graph)
					{
						if( Schema && Graph )
						{
							TSharedPtr<FEdGraphSchemaAction> Action = Schema->GetCreateCommentAction();

							if( Action.IsValid() )
							{
								Action->PerformAction(Graph, NULL, FVector2D());
							}
						}
					}
				};

				MenuBuilder->BeginSection("SchemaActionComment", LOCTEXT("MultiCommentHeader", "Comment Group"));
				MenuBuilder->AddMenuEntry(	LOCTEXT("MultiCommentDesc", "Create Comment from Selection"),
											LOCTEXT("CommentToolTip", "Create a resizable comment box around selection."),
											FSlateIcon(), 
											FUIAction( FExecuteAction::CreateStatic( SCommentUtility::CreateComment, this, const_cast<UEdGraph*>( CurrentGraph ))));
				MenuBuilder->EndSection();
			}
		}
	}
#endif
}

FString UEdGraphSchema::IsCurrentPinDefaultValid(const UEdGraphPin* Pin) const
{
	return IsPinDefaultValid(Pin, Pin->DefaultValue, Pin->DefaultObject, Pin->DefaultTextValue);
}

#undef LOCTEXT_NAMESPACE
