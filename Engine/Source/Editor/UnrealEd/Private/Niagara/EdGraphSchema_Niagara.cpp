// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "UnrealEd.h"

#include "BlueprintGraphDefinitions.h"
#include "GraphEditorSettings.h"
#include "NiagaraComponent.h"

#include "INiagaraEditor.h"
#include "INiagaraCompiler.h"

#define LOCTEXT_NAMESPACE "NiagaraSchema"

#define SNAP_GRID (16) // @todo ensure this is the same as SNodePanel::GetSnapGridSize()

const FLinearColor UEdGraphSchema_Niagara::NodeTitleColor_Attribute = FLinearColor::Green;
const FLinearColor UEdGraphSchema_Niagara::NodeTitleColor_Constant = FLinearColor::Red;
const FLinearColor UEdGraphSchema_Niagara::NodeTitleColor_SystemConstant = FLinearColor::White;
const FLinearColor UEdGraphSchema_Niagara::NodeTitleColor_FunctionCall = FLinearColor::Blue;

namespace 
{
	// Maximum distance a drag can be off a node edge to require 'push off' from node
	const int32 NodeDistance = 60;
}

UEdGraphNode* FNiagaraSchemaAction_NewNode::PerformAction(class UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode/* = true*/)
{
	UEdGraphNode* ResultNode = NULL;

	// If there is a template, we actually use it
	if (NodeTemplate != NULL)
	{
		NodeTemplate->SetFlags(RF_Transactional);

		// set outer to be the graph so it doesn't go away
		NodeTemplate->Rename(NULL, ParentGraph, REN_NonTransactional);
		ParentGraph->AddNode(NodeTemplate, true, bSelectNewNode);

		NodeTemplate->CreateNewGuid();
		NodeTemplate->PostPlacedNewNode();
		NodeTemplate->AllocateDefaultPins();
		NodeTemplate->AutowireNewNode(FromPin);

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

		NodeTemplate->NodePosX = XLocation;
		NodeTemplate->NodePosY = Location.Y;
		NodeTemplate->SnapToGrid(SNAP_GRID);

		ResultNode = NodeTemplate;
	}

	return ResultNode;
}

UEdGraphNode* FNiagaraSchemaAction_NewNode::PerformAction(class UEdGraph* ParentGraph, TArray<UEdGraphPin*>& FromPins, const FVector2D Location, bool bSelectNewNode/* = true*/) 
{
	UEdGraphNode* ResultNode = NULL;

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

	return ResultNode;
}

void FNiagaraSchemaAction_NewNode::AddReferencedObjects( FReferenceCollector& Collector )
{
	FEdGraphSchemaAction::AddReferencedObjects( Collector );

	// These don't get saved to disk, but we want to make sure the objects don't get GC'd while the action array is around
	Collector.AddReferencedObject( NodeTemplate );
}

//////////////////////////////////////////////////////////////////////////

UEdGraphSchema_Niagara::UEdGraphSchema_Niagara(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

	PC_Float = TEXT("float");
	PC_Vector = TEXT("vector");
	PC_Matrix = TEXT("matrix");
	PC_Curve = TEXT("curve");
}

TSharedPtr<FNiagaraSchemaAction_NewNode> AddNewNodeAction(FGraphContextMenuBuilder& ContextMenuBuilder, const FString& Category, const FText& MenuDesc, const FString& Tooltip)
{
	TSharedPtr<FNiagaraSchemaAction_NewNode> NewAction = TSharedPtr<FNiagaraSchemaAction_NewNode>(new FNiagaraSchemaAction_NewNode(Category, MenuDesc, Tooltip, 0));
	ContextMenuBuilder.AddAction( NewAction );
	return NewAction;
}

void UEdGraphSchema_Niagara::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
	const UNiagaraGraph* NiagaraGraph = CastChecked<UNiagaraGraph>(ContextMenuBuilder.CurrentGraph);

	//Add menu options for Attributes currently defined by the output node.
	const UNiagaraNodeOutput* OutNode = NiagaraGraph->FindOutputNode();
	if (OutNode)
	{
		for (int32 i = 0; i < OutNode->Outputs.Num(); i++)
		{
			const FNiagaraVariableInfo Attr = OutNode->Outputs[i];

			FFormatNamedArguments Args;
			Args.Add(TEXT("Attribute"), FText::FromName(Attr.Name));
			const FText MenuDesc = FText::Format(LOCTEXT("GetAttribute", "Get {Attribute}"), Args);

			TSharedPtr<FNiagaraSchemaAction_NewNode> GetAttrAction = AddNewNodeAction(ContextMenuBuilder, *LOCTEXT("Attributes Menu Title", "Attributes").ToString(), MenuDesc, TEXT(""));

			UNiagaraNodeInput* InputNode = NewObject<UNiagaraNodeInput>(ContextMenuBuilder.OwnerOfTemporaries);
			InputNode->Input = Attr;
			GetAttrAction->NodeTemplate = InputNode;
		}
	}

#define NiagaraOp(OPNAME) \
		if(const FNiagaraOpInfo* OpInfo = FNiagaraOpInfo::GetOpInfo(FNiagaraOpInfo::OPNAME))\
		{\
		TSharedPtr<FNiagaraSchemaAction_NewNode> AddOpAction = AddNewNodeAction(ContextMenuBuilder, *LOCTEXT("Operations Menu Title", "Operations").ToString(), OpInfo->FriendlyName, TEXT("")); \
			UNiagaraNodeOp* OpNode = NewObject<UNiagaraNodeOp>(ContextMenuBuilder.OwnerOfTemporaries); \
			OpNode->OpName = OpInfo->Name; \
			AddOpAction->NodeTemplate = OpNode; \
		}

	NiagaraOpList

#undef NiagaraOp

	//Emitter constants managed by the system.
	const TArray<FNiagaraVariableInfo>& SystemConstants = UNiagaraComponent::GetSystemConstants();
	for (const FNiagaraVariableInfo& SysConst : SystemConstants)
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("Constant"), FText::FromName(SysConst.Name));
		const FText MenuDesc = FText::Format(LOCTEXT("GetSystemConstant", "Get {Constant}"), Args);

		TSharedPtr<FNiagaraSchemaAction_NewNode> GetConstAction = AddNewNodeAction(ContextMenuBuilder, *LOCTEXT("System Constants Menu Title", "System Constants").ToString(), MenuDesc, TEXT(""));

		UNiagaraNodeInput* InputNode = NewObject<UNiagaraNodeInput>(ContextMenuBuilder.OwnerOfTemporaries);
		InputNode->Input = SysConst;
		InputNode->bExposeWhenConstant = false;
		InputNode->bCanBeExposed = false;
		GetConstAction->NodeTemplate = InputNode;
	}

	//Add a generic Input node to allow getting external constants the current context doesn't know about.
	{
		const FText MenuDesc = LOCTEXT("GetInput", "Input");

		//TSharedPtr<FNiagaraSchemaAction_NewNode> GetConstAction = AddNewNodeAction(ContextMenuBuilder, *LOCTEXT("System Constants Menu Title", "System Constants").ToString(), MenuDesc, TEXT(""));
		TSharedPtr<FNiagaraSchemaAction_NewNode> InputAction = AddNewNodeAction(ContextMenuBuilder, TEXT(""), MenuDesc, TEXT(""));

		UNiagaraNodeInput* InputNode = NewObject<UNiagaraNodeInput>(ContextMenuBuilder.OwnerOfTemporaries);
		InputNode->Input.Name = NAME_None;
		InputNode->Input.Type = ENiagaraDataType::Vector;
		InputAction->NodeTemplate = InputNode;
	}

	//Add a function call node
	{
		const FText MenuDesc = LOCTEXT("NiagaraFunctionCall", "Function Call");

		TSharedPtr<FNiagaraSchemaAction_NewNode> FunctionCallAction = AddNewNodeAction(ContextMenuBuilder, TEXT(""), MenuDesc, TEXT(""));

		UNiagaraNodeFunctionCall* FunctionCallNode = NewObject<UNiagaraNodeFunctionCall>(ContextMenuBuilder.OwnerOfTemporaries);
		FunctionCallAction->NodeTemplate = FunctionCallNode;
	}
	//TODO: Add quick commands for certain UNiagaraStructs and UNiagaraScripts to be added as functions
}

const FPinConnectionResponse UEdGraphSchema_Niagara::CanCreateConnection(const UEdGraphPin* PinA, const UEdGraphPin* PinB) const
{
	// Make sure the pins are not on the same node
	if (PinA->GetOwningNode() == PinB->GetOwningNode())
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Both are on the same node"));
	}

	// Check both pins support connections
	if(PinA->bNotConnectable || PinB->bNotConnectable)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Pin doesn't support connections."));
	}

	// Compare the directions
	const UEdGraphPin* InputPin = NULL;
	const UEdGraphPin* OutputPin = NULL;

	if (!CategorizePinsByDirection(PinA, PinB, /*out*/ InputPin, /*out*/ OutputPin))
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Directions are not compatible"));
	}

	// Types must match exactly
	if(PinA->PinType != PinB->PinType)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Types are not compatible"));
	}

	// See if we want to break existing connections (if its an input with an existing connection)
	const bool bBreakExistingDueToDataInput = (InputPin->LinkedTo.Num() > 0);
	if (bBreakExistingDueToDataInput)
	{
		const ECanCreateConnectionResponse ReplyBreakInputs = (PinA == InputPin) ? CONNECT_RESPONSE_BREAK_OTHERS_A : CONNECT_RESPONSE_BREAK_OTHERS_B;
		return FPinConnectionResponse(ReplyBreakInputs, TEXT("Replace existing input connections"));
	}
	else
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, TEXT(""));
	}
}

FLinearColor UEdGraphSchema_Niagara::GetPinTypeColor(const FEdGraphPinType& PinType) const
{
	const FString& TypeString = PinType.PinCategory;
	const UGraphEditorSettings* Settings = GetDefault<UGraphEditorSettings>();

	if (TypeString == PC_Float)
	{
		return Settings->FloatPinTypeColor;
	}
	else if (TypeString == PC_Vector)
	{
		return Settings->VectorPinTypeColor;
	}
	else if (TypeString == PC_Matrix)
	{
		return Settings->TransformPinTypeColor;
	}
	else if (TypeString == PC_Curve)
	{
		return Settings->StructPinTypeColor;
	}

	return Settings->DefaultPinTypeColor;
}

bool UEdGraphSchema_Niagara::ShouldHidePinDefaultValue(UEdGraphPin* Pin) const
{
	check(Pin != NULL);

	if (Pin->bDefaultValueIsIgnored)
	{
		return true;
	}

	return false;
}

ENiagaraDataType UEdGraphSchema_Niagara::GetPinType(UEdGraphPin* Pin) const
{
	if (Pin->PinType.PinCategory == PC_Float)
	{
		return ENiagaraDataType::Scalar;
	}
	else if (Pin->PinType.PinCategory == PC_Vector)
	{
		return ENiagaraDataType::Vector;
	}
	else if (Pin->PinType.PinCategory == PC_Matrix)
	{
		return ENiagaraDataType::Matrix;
	}
	else if (Pin->PinType.PinCategory == PC_Curve)
	{
		return ENiagaraDataType::Curve;
	}
	check(0);
	return ENiagaraDataType::Vector;
}

void UEdGraphSchema_Niagara::GetPinDefaultValue(UEdGraphPin* Pin, float& OutDefault)const
{
	//Ugh, this string parsing is rubbish. Surely there's a consistent api for dealing with default values in the edgraph without all this?
	FString ResultString = Pin->DefaultValue.IsEmpty() ? Pin->AutogeneratedDefaultValue : Pin->DefaultValue;
	ResultString.Trim();
	ResultString.TrimTrailing();
	OutDefault = FCString::Atof(*ResultString);
}

void UEdGraphSchema_Niagara::GetPinDefaultValue(UEdGraphPin* Pin, FVector4& OutDefault)const
{
	//Ugh, this string parsing is rubbish. Surely there's a consistent api for dealing with default values in the edgraph without all this?
	TArray<FString> ResultString;
	FString ValueString = Pin->DefaultValue.IsEmpty() ? Pin->AutogeneratedDefaultValue : Pin->DefaultValue;
	ValueString.Trim();
	ValueString.TrimTrailing();
	ValueString.ParseIntoArray(ResultString, TEXT(","), true);
	if (ResultString.Num() == 4)
	{
		OutDefault = FVector4(FCString::Atof(*ResultString[0]), FCString::Atof(*ResultString[1]), FCString::Atof(*ResultString[2]), FCString::Atof(*ResultString[3]));
	}
}

void UEdGraphSchema_Niagara::GetPinDefaultValue(UEdGraphPin* Pin, FMatrix& OutDefault)const
{
	TArray<FString> ResultString;
	FString ValueString = Pin->DefaultValue.IsEmpty() ? Pin->AutogeneratedDefaultValue : Pin->DefaultValue;
	ValueString.Trim();
	ValueString.TrimTrailing();
	ValueString.ParseIntoArray(ResultString, TEXT(","), true);
	check(ResultString.Num() == 16);
	if (ResultString.Num() == 16)
	{
		OutDefault.M[0][0] = FCString::Atof(*ResultString[0]); OutDefault.M[0][1] = FCString::Atof(*ResultString[1]); OutDefault.M[0][2] = FCString::Atof(*ResultString[2]); OutDefault.M[0][3] = FCString::Atof(*ResultString[3]);
		OutDefault.M[1][0] = FCString::Atof(*ResultString[4]); OutDefault.M[1][1] = FCString::Atof(*ResultString[5]); OutDefault.M[1][2] = FCString::Atof(*ResultString[6]); OutDefault.M[1][3] = FCString::Atof(*ResultString[7]);
		OutDefault.M[2][0] = FCString::Atof(*ResultString[8]); OutDefault.M[2][1] = FCString::Atof(*ResultString[9]); OutDefault.M[2][2] = FCString::Atof(*ResultString[10]); OutDefault.M[2][3] = FCString::Atof(*ResultString[11]);
		OutDefault.M[3][0] = FCString::Atof(*ResultString[12]); OutDefault.M[3][1] = FCString::Atof(*ResultString[13]); OutDefault.M[3][2] = FCString::Atof(*ResultString[14]); OutDefault.M[3][3] = FCString::Atof(*ResultString[15]);
	}
}

bool UEdGraphSchema_Niagara::IsSystemConstant(const FNiagaraVariableInfo& Variable)const
{
	return UNiagaraComponent::GetSystemConstants().Find(Variable) != INDEX_NONE;
}

#undef LOCTEXT_NAMESPACE