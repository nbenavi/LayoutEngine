// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "UnrealEd.h"

#include "MainFrame.h"
#include "ModuleManager.h"
#include "DirectoryWatcherModule.h"
#include "../../../DataTableEditor/Public/IDataTableEditor.h"
#include "Curves/CurveVector.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveLinearColor.h"
#include "Engine/DataTable.h"
#include "Engine/CurveTable.h"
#include "Engine/UserDefinedStruct.h"
#include "SCSVImportOptions.h"
#include "DataTableEditorUtils.h"
DEFINE_LOG_CATEGORY(LogCSVImportFactory);

#define LOCTEXT_NAMESPACE "CSVImportFactory"

//////////////////////////////////////////////////////////////////////////

static UClass* GetCurveClass( ECSVImportType ImportType )
{
	switch( ImportType )
	{
	case ECSVImportType::ECSV_CurveFloat:
		return UCurveFloat::StaticClass();
		break;
	case ECSVImportType::ECSV_CurveVector:
		return UCurveVector::StaticClass();
		break;
	case ECSVImportType::ECSV_CurveLinearColor:
		return UCurveLinearColor::StaticClass();
		break;
	default:
		return UCurveVector::StaticClass();
		break;
	}
}


UCSVImportFactory::UCSVImportFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

	bCreateNew = false;
	bEditAfterNew = true;
	SupportedClass = UDataTable::StaticClass();

	bEditorImport = true;
	bText = true;

	Formats.Add(TEXT("csv;Comma-separated values"));
}

FText UCSVImportFactory::GetDisplayName() const
{
	return LOCTEXT("CSVImportFactoryDescription", "Comma Separated Values");
}


bool UCSVImportFactory::DoesSupportClass(UClass * Class)
{
	return (Class == UDataTable::StaticClass() || Class == UCurveTable::StaticClass() || Class == UCurveFloat::StaticClass() || Class == UCurveVector::StaticClass() || Class == UCurveLinearColor::StaticClass() );
}

UObject* UCSVImportFactory::FactoryCreateText( UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, const TCHAR* Type, const TCHAR*& Buffer, const TCHAR* BufferEnd, FFeedbackContext* Warn )
{
	FEditorDelegates::OnAssetPreImport.Broadcast(this, InClass, InParent, InName, Type);

	// See if table/curve already exists
	UDataTable* ExistingTable = FindObject<UDataTable>(InParent, *InName.ToString());
	UCurveTable* ExistingCurveTable = FindObject<UCurveTable>(InParent, *InName.ToString());
	UCurveBase* ExistingCurve = FindObject<UCurveBase>(InParent, *InName.ToString());

	// Save off information if so
	bool bHaveInfo = false;
	UScriptStruct* ImportRowStruct = NULL;
	ERichCurveInterpMode ImportCurveInterpMode = RCIM_Linear;

	ECSVImportType ImportType = ECSVImportType::ECSV_DataTable;
	if(ExistingTable != NULL)
	{
		ImportRowStruct = ExistingTable->RowStruct;
		bHaveInfo = true;
	}
	else if(ExistingCurveTable != NULL)
	{
		ImportType = ECSVImportType::ECSV_CurveTable;
		bHaveInfo = true;
	}
	else if(ExistingCurve != NULL)
	{
		ImportType = ExistingCurve->IsA(UCurveFloat::StaticClass()) ? ECSVImportType::ECSV_CurveFloat : ECSVImportType::ECSV_CurveVector;
		bHaveInfo = true;
	}

	bool bDoImport = true;

	// If we do not have the info we need, pop up window to ask for things
	if(!bHaveInfo)
	{
		TSharedPtr<SWindow> ParentWindow;
		// Check if the main frame is loaded.  When using the old main frame it may not be.
		if( FModuleManager::Get().IsModuleLoaded( "MainFrame" ) )
		{
			IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>( "MainFrame" );
			ParentWindow = MainFrame.GetParentWindow();
		}

		TSharedPtr<SCSVImportOptions> ImportOptionsWindow;

		TSharedRef<SWindow> Window = SNew(SWindow)
			.Title( LOCTEXT("DataTableOptionsWindowTitle", "DataTable Options" ))
			.SizingRule( ESizingRule::Autosized );
		
		Window->SetContent
		(
			SAssignNew(ImportOptionsWindow, SCSVImportOptions)
			.WidgetWindow(Window)
		);

		FSlateApplication::Get().AddModalWindow(Window, ParentWindow, false);

		ImportType = ImportOptionsWindow->GetSelectedImportType();
		ImportRowStruct = ImportOptionsWindow->GetSelectedRowStruct();
		ImportCurveInterpMode = ImportOptionsWindow->GetSelectedCurveIterpMode();
		bDoImport = ImportOptionsWindow->ShouldImport();
	}

	UObject* NewAsset = NULL;
	if(bDoImport)
	{
		// Convert buffer to an FString (will this be slow with big tables?)
		FString String;
		//const int32 BufferSize = BufferEnd - Buffer;
		//appBufferToString( String, Buffer, BufferSize );
		int32 NumChars = (BufferEnd - Buffer);
		TArray<TCHAR>& StringChars = String.GetCharArray();
		StringChars.AddUninitialized(NumChars+1);
		FMemory::Memcpy(StringChars.GetData(), Buffer, NumChars*sizeof(TCHAR));
		StringChars.Last() = 0;

		TArray<FString> Problems;

		if (ImportType == ECSVImportType::ECSV_DataTable)
		{
			// If there is an existing table, need to call this to free data memory before recreating object
			if(ExistingTable != NULL)
			{
				ExistingTable->EmptyTable();
			}

			// Create/reset table
			UDataTable* NewTable = NewObject<UDataTable>(InParent, InName, Flags);
			NewTable->RowStruct = ImportRowStruct;
			NewTable->ImportPath = FReimportManager::SanitizeImportFilename(CurrentFilename, NewTable);
			// Go ahead and create table from string
			Problems = DoImportDataTable(NewTable, String);

			// Print out
			UE_LOG(LogCSVImportFactory, Log, TEXT("Imported DataTable '%s' - %d Problems"), *InName.ToString(), Problems.Num());
			NewAsset = NewTable;
		}
		else if (ImportType == ECSVImportType::ECSV_CurveTable)
		{
			// If there is an existing table, need to call this to free data memory before recreating object
			if(ExistingCurveTable != NULL)
			{
				ExistingCurveTable->EmptyTable();
			}

			// Create/reset table
			UCurveTable* NewTable = NewObject<UCurveTable>(InParent, InName, Flags);
			NewTable->ImportPath = FReimportManager::SanitizeImportFilename(CurrentFilename, NewTable);

			// Go ahead and create table from string
			Problems = DoImportCurveTable(NewTable, String, ImportCurveInterpMode);

			// Print out
			UE_LOG(LogCSVImportFactory, Log, TEXT("Imported CurveTable '%s' - %d Problems"), *InName.ToString(), Problems.Num());
			NewAsset = NewTable;
		}
		else if (ImportType == ECSVImportType::ECSV_CurveFloat || ImportType == ECSVImportType::ECSV_CurveVector || ImportType == ECSVImportType::ECSV_CurveLinearColor)
		{
			UClass* CurveClass = GetCurveClass( ImportType );

			// Create/reset curve
			UCurveBase* NewCurve = NewObject<UCurveBase>(InParent, CurveClass, InName, Flags);

			Problems = DoImportCurve(NewCurve, String);

			UE_LOG(LogCSVImportFactory, Log, TEXT("Imported Curve '%s' - %d Problems"), *InName.ToString(), Problems.Num());
			NewCurve->ImportPath = FReimportManager::SanitizeImportFilename(CurrentFilename, NewCurve);
			NewAsset = NewCurve;
		}
		
		if(Problems.Num() > 0)
		{
			FString AllProblems;

			for(int32 ProbIdx=0; ProbIdx<Problems.Num(); ProbIdx++)
			{
				// Output problems to log
				UE_LOG(LogCSVImportFactory, Log, TEXT("%d:%s"), ProbIdx, *Problems[ProbIdx]);
				AllProblems += Problems[ProbIdx];
				AllProblems += TEXT("\n");
			}

			// Pop up any problems for user
			FMessageDialog::Open( EAppMsgType::Ok, FText::FromString( AllProblems ) );
		}
	}

	FEditorDelegates::OnAssetPostImport.Broadcast(this, NewAsset);

	return NewAsset;
}

bool UCSVImportFactory::ReimportCSV( UObject* Obj )
{
	bool bHandled = false;
	if(UCurveBase* Curve = Cast<UCurveBase>(Obj))
	{
		bHandled = Reimport(Curve, FReimportManager::ResolveImportFilename(Curve->ImportPath, Curve));
	}
	else if(UCurveTable* CurveTable = Cast<UCurveTable>(Obj))
	{
		bHandled = Reimport(CurveTable, FReimportManager::ResolveImportFilename(CurveTable->ImportPath, CurveTable));
	}
	else if(UDataTable* DataTable = Cast<UDataTable>(Obj))
	{
		bHandled = Reimport(DataTable, FReimportManager::ResolveImportFilename(DataTable->ImportPath, DataTable));
	}
	return bHandled;
}

bool UCSVImportFactory::Reimport( UObject* Obj, const FString& Path )
{
	if(Path.IsEmpty() == false)
	{
		FString FilePath = IFileManager::Get().ConvertToRelativePath(*Path);

		FString Data;
		if( FFileHelper::LoadFileToString( Data, *FilePath) )
		{
			const TCHAR* Ptr = *Data;
			CurrentFilename = FilePath; //not thread safe but seems to be how it is done..
			auto Result = FactoryCreateText( Obj->GetClass(), Obj->GetOuter(), Obj->GetFName(), Obj->GetFlags(), NULL, *FPaths::GetExtension(FilePath), Ptr, Ptr+Data.Len(), NULL );
			return true;
		}
	}
	return false;
}

TArray<FString> UCSVImportFactory::DoImportDataTable(UDataTable* TargetDataTable, const FString& DataToImport)
{
	// Are we importing JSON data?
	const bool bIsJSON = CurrentFilename.EndsWith(TEXT(".json"));
	if (bIsJSON)
	{
		return TargetDataTable->CreateTableFromJSONString(DataToImport);
	}

	return TargetDataTable->CreateTableFromCSVString(DataToImport);
}

TArray<FString> UCSVImportFactory::DoImportCurveTable(UCurveTable* TargetCurveTable, const FString& DataToImport, const ERichCurveInterpMode ImportCurveInterpMode)
{
	// Are we importing JSON data?
	const bool bIsJSON = CurrentFilename.EndsWith(TEXT(".json"));
	if (bIsJSON)
	{
		return TargetCurveTable->CreateTableFromJSONString(DataToImport, ImportCurveInterpMode);
	}

	return TargetCurveTable->CreateTableFromCSVString(DataToImport, ImportCurveInterpMode);
}

TArray<FString> UCSVImportFactory::DoImportCurve(UCurveBase* TargetCurve, const FString& DataToImport)
{
	// Are we importing JSON data?
	const bool bIsJSON = CurrentFilename.EndsWith(TEXT(".json"));
	if (bIsJSON)
	{
		TArray<FString> Result;
		Result.Add(LOCTEXT("Error_CannotImportCurveFromJSON", "Cannot import a curve from JSON. Please use CSV instead.").ToString());
		return Result;
	}

	return TargetCurve->CreateCurveFromCSVString(DataToImport);
}

//////////////////////////////////////////////////////////////////////////

UReimportDataTableFactory::UReimportDataTableFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Formats.Add(TEXT("json;JavaScript Object Notation"));
}

bool UReimportDataTableFactory::CanReimport( UObject* Obj, TArray<FString>& OutFilenames )
{	
	UDataTable* DataTable = Cast<UDataTable>(Obj);
	if(DataTable)
	{
		OutFilenames.Add(FReimportManager::ResolveImportFilename(DataTable->ImportPath, DataTable));
		return true;
	}
	return false;
}

void UReimportDataTableFactory::SetReimportPaths( UObject* Obj, const TArray<FString>& NewReimportPaths )
{	
	UDataTable* DataTable = Cast<UDataTable>(Obj);
	if(DataTable && ensure(NewReimportPaths.Num() == 1))
	{
		DataTable->ImportPath = FReimportManager::SanitizeImportFilename(NewReimportPaths[0], DataTable);
	}
}

EReimportResult::Type UReimportDataTableFactory::Reimport( UObject* Obj )
{	
	auto Result = EReimportResult::Failed;
	if(auto DataTable = Cast<UDataTable>(Obj))
	{
		FDataTableEditorUtils::BroadcastPreChange(DataTable, FDataTableEditorUtils::EDataTableChangeInfo::RowList);
		Result = UCSVImportFactory::ReimportCSV(DataTable) ? EReimportResult::Succeeded : EReimportResult::Failed;
		FDataTableEditorUtils::BroadcastPostChange(DataTable, FDataTableEditorUtils::EDataTableChangeInfo::RowList);
	}
	return Result;
}

int32 UReimportDataTableFactory::GetPriority() const
{
	return ImportPriority;
}

////////////////////////////////////////////////////////////////////////////
//
UReimportCurveTableFactory::UReimportCurveTableFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	Formats.Add(TEXT("json;JavaScript Object Notation"));
}

bool UReimportCurveTableFactory::CanReimport( UObject* Obj, TArray<FString>& OutFilenames )
{	
	UCurveTable* CurveTable = Cast<UCurveTable>(Obj);
	if(CurveTable)
	{
		OutFilenames.Add(FReimportManager::ResolveImportFilename(CurveTable->ImportPath, CurveTable));
		return true;
	}
	return false;
}

void UReimportCurveTableFactory::SetReimportPaths( UObject* Obj, const TArray<FString>& NewReimportPaths )
{	
	UCurveTable* CurveTable = Cast<UCurveTable>(Obj);
	if(CurveTable && ensure(NewReimportPaths.Num() == 1))
	{
		CurveTable->ImportPath = FReimportManager::SanitizeImportFilename(NewReimportPaths[0], CurveTable);
	}
}

EReimportResult::Type UReimportCurveTableFactory::Reimport( UObject* Obj )
{	
	if(Cast<UCurveTable>(Obj))
	{
		return UCSVImportFactory::ReimportCSV(Obj) ? EReimportResult::Succeeded : EReimportResult::Failed;
	}
	return EReimportResult::Failed;
}

int32 UReimportCurveTableFactory::GetPriority() const
{
	return ImportPriority;
}

////////////////////////////////////////////////////////////////////////////
//
UReimportCurveFactory::UReimportCurveFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool UReimportCurveFactory::CanReimport( UObject* Obj, TArray<FString>& OutFilenames )
{	
	UCurveBase* CurveBase = Cast<UCurveBase>(Obj);
	if(CurveBase)
	{
		OutFilenames.Add(FReimportManager::ResolveImportFilename(CurveBase->ImportPath, CurveBase));
		return true;
	}
	return false;
}

void UReimportCurveFactory::SetReimportPaths( UObject* Obj, const TArray<FString>& NewReimportPaths )
{	
	UCurveBase* CurveBase = Cast<UCurveBase>(Obj);
	if(CurveBase && ensure(NewReimportPaths.Num() == 1))
	{
		CurveBase->ImportPath = FReimportManager::SanitizeImportFilename(NewReimportPaths[0], CurveBase);
	}
}

EReimportResult::Type UReimportCurveFactory::Reimport( UObject* Obj )
{	
	if(Cast<UCurveBase>(Obj))
	{
		return UCSVImportFactory::ReimportCSV(Obj) ? EReimportResult::Succeeded : EReimportResult::Failed;
	}
	return EReimportResult::Failed;
}

int32 UReimportCurveFactory::GetPriority() const
{
	return ImportPriority;
}


#undef LOCTEXT_NAMESPACE
