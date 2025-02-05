// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "CoreUObjectPrivate.h"
#include "PropertyHelper.h"
#include "LinkerPlaceholderClass.h"
#include "LinkerPlaceholderExportObject.h"

/*-----------------------------------------------------------------------------
	UObjectPropertyBase.
-----------------------------------------------------------------------------*/

void UObjectPropertyBase::BeginDestroy()
{
#if USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING
	if (ULinkerPlaceholderClass* PlaceholderClass = Cast<ULinkerPlaceholderClass>(PropertyClass))
	{
		PlaceholderClass->RemoveReferencingProperty(this);
	}
#endif // USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING

	Super::BeginDestroy();
}

void UObjectPropertyBase::InstanceSubobjects(void* Data, void const* DefaultData, UObject* Owner, FObjectInstancingGraph* InstanceGraph )
{
	for ( int32 ArrayIndex = 0; ArrayIndex < ArrayDim; ArrayIndex++ )
	{
		UObject* CurrentValue = GetObjectPropertyValue((uint8*)Data + ArrayIndex * ElementSize);
		if ( CurrentValue )
		{
			UObject *SubobjectTemplate = DefaultData ? GetObjectPropertyValue((uint8*)DefaultData + ArrayIndex * ElementSize): NULL;
			UObject* NewValue = InstanceGraph->InstancePropertyValue(SubobjectTemplate, CurrentValue, Owner, HasAnyPropertyFlags(CPF_Transient), HasAnyPropertyFlags(CPF_InstancedReference));
			SetObjectPropertyValue((uint8*)Data + ArrayIndex * ElementSize, NewValue);
		}
	}
}

bool UObjectPropertyBase::Identical( const void* A, const void* B, uint32 PortFlags ) const
{
	UObject* ObjectA = A ? GetObjectPropertyValue(A) : NULL;
	UObject* ObjectB = B ? GetObjectPropertyValue(B) : NULL;
	if (!ObjectA && !ObjectB)
	{
		return true;
	}
	if (!ObjectA || !ObjectB)
	{
		return false;
	}
	// Compare actual pointers. We don't do this during PIE because we want to be sure to serialize everything. An example is the LevelScriptActor being serialized against its CDO,
	// which contains actor references. We want to serialize those references so they are fixed up.
	const bool bDuplicatingForPIE = (PortFlags&PPF_DuplicateForPIE) != 0;
	bool bResult = !bDuplicatingForPIE ? (ObjectA == ObjectB) : false;
	// always serialize the cross level references, because they could be NULL
	// @todo: okay, this is pretty hacky overall - we should have a PortFlag or something
	// that is set during SavePackage. Other times, we don't want to immediately return false
	// (instead of just this ExportDefProps case)
	// instance testing
	if (!bResult && ObjectA->GetClass() == ObjectB->GetClass())
	{
		bool bPerformDeepComparison = (PortFlags&PPF_DeepComparison) != 0;
		if ((PortFlags&PPF_DeepCompareInstances) && !bPerformDeepComparison)
		{
			bPerformDeepComparison = ObjectA->IsTemplate() != ObjectB->IsTemplate();
		}

		if (!bResult && bPerformDeepComparison)
		{
			// In order for deep comparison to be match they both need to have the same name and that name needs to be included in the instancing table for the class
			if (ObjectA->GetFName() == ObjectB->GetFName() && ObjectA->GetClass()->GetDefaultSubobjectByName(ObjectA->GetFName()))
			{
				checkSlow(ObjectA->IsDefaultSubobject() && ObjectB->IsDefaultSubobject() && ObjectA->GetClass()->GetDefaultSubobjectByName(ObjectA->GetFName()) == ObjectB->GetClass()->GetDefaultSubobjectByName(ObjectB->GetFName())); // equivalent
				bResult = AreInstancedObjectsIdentical(ObjectA,ObjectB,PortFlags);
			}
		}
	}
	return bResult;
}

bool UObjectPropertyBase::NetSerializeItem( FArchive& Ar, UPackageMap* Map, void* Data, TArray<uint8> * MetaData ) const
{
	UObject* Object = GetObjectPropertyValue(Data);
	bool Result = Map->SerializeObject( Ar, PropertyClass, Object );
	SetObjectPropertyValue(Data, Object);
	return Result;
}
void UObjectPropertyBase::Serialize( FArchive& Ar )
{
	Super::Serialize( Ar );
	Ar << PropertyClass;

#if USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING
	if (Ar.IsLoading() || Ar.IsObjectReferenceCollector())
	{
		if (ULinkerPlaceholderClass* PlaceholderClass = Cast<ULinkerPlaceholderClass>(PropertyClass))
		{
			PlaceholderClass->AddReferencingProperty(this);
		}
	}
#endif // USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING
}

#if USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING
void UObjectPropertyBase::SetPropertyClass(UClass* NewPropertyClass)
{
	if (ULinkerPlaceholderClass* NewPlaceholderClass = Cast<ULinkerPlaceholderClass>(NewPropertyClass))
	{
		NewPlaceholderClass->AddReferencingProperty(this);
	}
	
	if (ULinkerPlaceholderClass* OldPlaceholderClass = Cast<ULinkerPlaceholderClass>(PropertyClass))
	{
		OldPlaceholderClass->RemoveReferencingProperty(this);
	}
	PropertyClass = NewPropertyClass;
}
#endif // USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING

void UObjectPropertyBase::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{	
	UObjectPropertyBase* This = CastChecked<UObjectPropertyBase>(InThis);
	Collector.AddReferencedObject( This->PropertyClass, This );
	Super::AddReferencedObjects( This, Collector );
}

FString UObjectPropertyBase::GetExportPath(const UObject* Object, const UObject* Parent, const UObject* ExportRootScope, const uint32 PortFlags)
{
	bool bExportFullyQualified = true;

	// When exporting from one package or graph to another package or graph, we don't want to fully qualify the name, as it may refer
	// to a level or graph that doesn't exist or cause a linkage to a node in a different graph
	const UObject* StopOuter = nullptr;
	if (PortFlags & PPF_ExportsNotFullyQualified)
	{
		StopOuter = (ExportRootScope || (Parent == nullptr)) ? ExportRootScope : Parent->GetOutermost();
		bExportFullyQualified = !Object->IsIn(StopOuter);
	}

	// if we want a full qualified object reference, use the pathname, otherwise, use just the object name
	if (bExportFullyQualified)
	{
		StopOuter = nullptr;
		if ( (PortFlags&PPF_SimpleObjectText) != 0 && Parent != nullptr )
		{
			StopOuter = Parent->GetOutermost();
		}
	}
	else if (Parent != NULL && Object->IsIn(Parent))
	{
		StopOuter = Parent;
	}

	// Take the path name relative to the stopping point outermost ptr.
	// This is so that cases like a component referencing a component in another actor work correctly when pasted
	FString PathName = Object->GetPathName(StopOuter);
	int32 ResultIdx = 0;
	// Object names that contain invalid characters and paths that contain spaces must be put into quotes to be handled correctly
	if ( (PortFlags & PPF_Delimited) && (!Object->GetFName().IsValidXName(INVALID_OBJECTNAME_CHARACTERS) || PathName.FindChar(' ', ResultIdx)) )
	{
		PathName = FString::Printf(TEXT("\"%s\""), *PathName.ReplaceQuotesWithEscapedQuotes());
	}
	return FString::Printf( TEXT("%s'%s'"), *Object->GetClass()->GetName(), *PathName );
}

void UObjectPropertyBase::ExportTextItem( FString& ValueStr, const void* PropertyValue, const void* DefaultValue, UObject* Parent, int32 PortFlags, UObject* ExportRootScope ) const
{
	UObject* Temp = GetObjectPropertyValue(PropertyValue);
	if( Temp != NULL )
	{
		if (PortFlags & PPF_DebugDump)
		{
			ValueStr += Temp->GetFullName();
		}
		else if (Parent && !Parent->HasAnyFlags(RF_ClassDefaultObject) && Temp->IsDefaultSubobject())
		{
			if ((PortFlags & PPF_Delimited) && (!Temp->GetFName().IsValidXName(INVALID_OBJECTNAME_CHARACTERS)))
			{
				ValueStr += FString::Printf(TEXT("\"%s\""), *Temp->GetName().ReplaceQuotesWithEscapedQuotes());
			}
			else
			{
				ValueStr += Temp->GetName();
			}
		}
		else
		{
			ValueStr += GetExportPath(Temp, Parent, ExportRootScope, PortFlags);
		}
	}
	else
	{
		ValueStr += TEXT("None");
	}
}

/**
 * Parses a text buffer into an object reference.
 *
 * @param	Property			the property that the value is being importing to
 * @param	OwnerObject			the object that is importing the value; used for determining search scope.
 * @param	RequiredMetaClass	the meta-class for the object to find; if the object that is resolved is not of this class type, the result is NULL.
 * @param	PortFlags			bitmask of EPropertyPortFlags that can modify the behavior of the search
 * @param	Buffer				the text to parse; should point to a textual representation of an object reference.  Can be just the object name (either fully 
 *								fully qualified or not), or can be formatted as a const object reference (i.e. SomeClass'SomePackage.TheObject')
 *								When the function returns, Buffer will be pointing to the first character after the object value text in the input stream.
 * @param	ResolvedValue		receives the object that is resolved from the input text.
 *
 * @return	true if the text is successfully resolved into a valid object reference of the correct type, false otherwise.
 */
bool UObjectPropertyBase::ParseObjectPropertyValue( const UProperty* Property, UObject* OwnerObject, UClass* RequiredMetaClass, uint32 PortFlags, const TCHAR*& Buffer, UObject*& out_ResolvedValue )
{
	check(Property);
	check(RequiredMetaClass);

 	const TCHAR* InBuffer = Buffer;

	FString Temp;
	Buffer = UPropertyHelpers::ReadToken(Buffer, Temp, true);
	if ( Buffer == NULL )
	{
		return false;
	}

	if ( Temp == TEXT("None") )
	{
		out_ResolvedValue = NULL;
	}
	else
	{
		UClass*	ObjectClass = RequiredMetaClass;

		SkipWhitespace(Buffer);

		bool bWarnOnNULL = (PortFlags&PPF_CheckReferences)!=0;

		if( *Buffer == TCHAR('\'') )
		{
			FString ObjectText;
			Buffer = UPropertyHelpers::ReadToken( ++Buffer, ObjectText, true );
			if( Buffer == NULL )
			{
				return false;
			}

			if( *Buffer++ != TCHAR('\'') )
			{
				return false;
			}

			// ignore the object class, it isn't fully qualified, and searching ANY_PACKAGE might get the wrong one!
			// Try the find the object.
			out_ResolvedValue = UObjectPropertyBase::FindImportedObject(Property, OwnerObject, ObjectClass, RequiredMetaClass, *ObjectText, PortFlags);
		}
		else
		{
			// Try the find the object.
			out_ResolvedValue = UObjectPropertyBase::FindImportedObject(Property, OwnerObject, ObjectClass, RequiredMetaClass, *Temp, PortFlags);
		}

		if ( out_ResolvedValue != NULL && !out_ResolvedValue->GetClass()->IsChildOf(RequiredMetaClass) )
		{
			if (bWarnOnNULL )
			{
				UE_LOG(LogProperty, Error, TEXT("%s: bad cast in '%s'"), *Property->GetFullName(), InBuffer );
			}

			out_ResolvedValue = NULL;
			return false;
		}

		// If we couldn't find it or load it, we'll have to do without it.
		if ( out_ResolvedValue == NULL )
		{
			if( bWarnOnNULL )
			{
				UE_LOG(LogProperty, Warning, TEXT("%s: unresolved reference to '%s'"), *Property->GetFullName(), InBuffer );
			}
			return false;
		}
	}

	return true;
}

const TCHAR* UObjectPropertyBase::ImportText_Internal( const TCHAR* InBuffer, void* Data, int32 PortFlags, UObject* Parent, FOutputDevice* ErrorText ) const
{
	const TCHAR* Buffer = InBuffer;
	UObject* Result = NULL;

	bool bOk = ParseObjectPropertyValue(this, Parent, PropertyClass, PortFlags, Buffer, Result);

	SetObjectPropertyValue(Data, Result);
	return Buffer;
}

UObject* UObjectPropertyBase::FindImportedObject( const UProperty* Property, UObject* OwnerObject, UClass* ObjectClass, UClass* RequiredMetaClass, const TCHAR* Text, uint32 PortFlags/*=0*/ )
{
	UObject*	Result = NULL;
	check( ObjectClass->IsChildOf(RequiredMetaClass) );

	bool AttemptNonQualifiedSearch = (PortFlags & PPF_AttemptNonQualifiedSearch) != 0; 

	// if we are importing default properties, first look for a matching subobject by
	// looking through the archetype chain at each outer and stop once the outer chain reaches the owning class's default object
	if (PortFlags & PPF_ParsingDefaultProperties)
	{
		for (UObject* SearchStart = OwnerObject; Result == NULL && SearchStart != NULL; SearchStart = SearchStart->GetOuter())
		{
			UObject* ScopedSearchRoot = SearchStart;
			while (Result == NULL && ScopedSearchRoot != NULL)
			{
				Result = StaticFindObject(ObjectClass, ScopedSearchRoot, Text);
				// don't think it's possible to get a non-subobject here, but it doesn't hurt to check
				if (Result != NULL && !Result->IsTemplate(RF_ClassDefaultObject))
				{
					Result = NULL;
				}

				ScopedSearchRoot = ScopedSearchRoot->GetArchetype();
			}
			if (SearchStart->HasAnyFlags(RF_ClassDefaultObject))
			{
				break;
			}
		}
	}
	
	// if we have a parent, look in the parent, then it's outer, then it's outer, ... 
	// this is because exported object properties that point to objects in the level aren't
	// fully qualified, and this will step up the nested object chain to solve any name
	// collisions within a nested object tree
	UObject* ScopedSearchRoot = OwnerObject;
	while (Result == NULL && ScopedSearchRoot != NULL)
	{
		Result = StaticFindObject(ObjectClass, ScopedSearchRoot, Text);
		// disallow class default subobjects here while importing defaults
		// this prevents the use of a subobject name that doesn't exist in the scope of the default object being imported
		// from grabbing some other subobject with the same name and class in some other arbitrary default object
		if (Result != NULL && (PortFlags & PPF_ParsingDefaultProperties) && Result->IsTemplate(RF_ClassDefaultObject))
		{
			Result = NULL;
		}

		ScopedSearchRoot = ScopedSearchRoot->GetOuter();
	}

	if (Result == NULL)
	{
		// attempt to find a fully qualified object
		Result = StaticFindObject(ObjectClass, NULL, Text);

		if (Result == NULL)
		{
			// match any object of the correct class whose path contains the specified path
			Result = StaticFindObject(ObjectClass, ANY_PACKAGE, Text);
			// disallow class default subobjects here while importing defaults
			if (Result != NULL && (PortFlags & PPF_ParsingDefaultProperties) && Result->IsTemplate(RF_ClassDefaultObject))
			{
				Result = NULL;
			}
		}
	}

	// if we haven;t found it yet, then try to find it without a qualified name
	if (!Result)
	{
		const TCHAR* Dot = FCString::Strrchr(Text, '.');
		if (Dot && AttemptNonQualifiedSearch)
		{
			// search with just the object name
			Result = FindImportedObject(Property, OwnerObject, ObjectClass, RequiredMetaClass, Dot + 1);
		}
		FString NewText(Text);
		// if it didn't have a dot, then maybe they just gave a uasset package name
		if (!Dot && !Result)
		{
			int32 LastSlash = NewText.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			if (LastSlash >= 0)
			{
				NewText += TEXT(".");
				NewText += (Text + LastSlash + 1);
				Dot = FCString::Strrchr(*NewText, '.');
			}
		}
		// If we still can't find it, try to load it. (Only try to load fully qualified names)
		if(!Result && Dot)
		{
			uint32 LoadFlags = LOAD_NoWarn | LOAD_FindIfFail;

			UE_LOG(LogProperty, Verbose, TEXT("FindImportedObject is attempting to import [%s] (class = %s) with StaticLoadObject"), Text, *GetFullNameSafe(ObjectClass));
			Result = StaticLoadObject(ObjectClass, NULL, Text, NULL, LoadFlags, NULL);
		}
	}

	// if we found an object, and we have a parent, make sure we are in the same package if the found object is private, unless it's a cross level property
	if (Result && !Result->HasAnyFlags(RF_Public) && OwnerObject && Result->GetOutermost() != OwnerObject->GetOutermost())
	{
		const UObjectPropertyBase* ObjectProperty = dynamic_cast<const UObjectPropertyBase*>(Property);
		if ( !ObjectProperty || !ObjectProperty->AllowCrossLevel())
		{
			UE_LOG(LogProperty, Warning, TEXT("Illegal TEXT reference to a private object in external package (%s) from referencer (%s).  Import failed..."), *Result->GetFullName(), *OwnerObject->GetFullName());
			Result = NULL;
		}
	}

	check(!Result || Result->IsA(RequiredMetaClass));
	return Result;
}

FName UObjectPropertyBase::GetID() const
{
	return NAME_ObjectProperty;
}

void UObjectPropertyBase::CheckValidObject(void* Value) const
{
	UObject *Object = GetObjectPropertyValue(Value);
	if (Object)
	{
		//
		// here we want to make sure the the object value still matches the 
		// object type expected by the property...

		UClass* ObjectClass = Object->GetClass();
		// we could be in the middle of replacing references to the 
		// PropertyClass itself (in the middle of an FArchiveReplaceObjectRef 
		// pass)... if this is the case, then we might have already replaced 
		// the object's class, but not the PropertyClass yet (or vise-versa)... 
		// so we use this to ensure, in that situation, that we don't clear the 
		// object value (if CLASS_NewerVersionExists is set, then we are likely 
		// in the middle of an FArchiveReplaceObjectRef pass)
		bool bIsReplacingClassRefs = PropertyClass && PropertyClass->HasAnyClassFlags(CLASS_NewerVersionExists) != ObjectClass->HasAnyClassFlags(CLASS_NewerVersionExists);
		
#if USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING
		FLinkerLoad* PropertyLinker = GetLinker();
		bool const bIsDeferringValueLoad = ((PropertyLinker == nullptr) || (PropertyLinker->LoadFlags & LOAD_DeferDependencyLoads)) &&
			Object->IsA<ULinkerPlaceholderExportObject>();

#if USE_DEFERRED_DEPENDENCY_CHECK_VERIFICATION_TESTS
		check(bIsDeferringValueLoad || !Object->IsA<ULinkerPlaceholderExportObject>());
#endif // USE_DEFERRED_DEPENDENCY_CHECK_VERIFICATION_TESTS

#else  // USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING 
		bool const bIsDeferringValueLoad = false;
#endif // USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING

		if ((PropertyClass != nullptr) && !ObjectClass->IsChildOf(PropertyClass) && !bIsReplacingClassRefs && !bIsDeferringValueLoad)
		{
			UE_LOG(LogProperty, Warning,
				TEXT("Serialized %s for a property of %s. Reference will be NULLed.\n    Property = %s\n    Item = %s"),
				*Object->GetClass()->GetFullName(),
				*PropertyClass->GetFullName(),
				*GetFullName(),
				*Object->GetFullName()
				);
			SetObjectPropertyValue(Value, NULL);
		}
	}
}

bool UObjectPropertyBase::SameType(const UProperty* Other) const
{
	return Super::SameType(Other) && (PropertyClass == ((UObjectPropertyBase*)Other)->PropertyClass);
}

IMPLEMENT_CORE_INTRINSIC_CLASS(UObjectPropertyBase, UProperty,
	{
		Class->EmitObjectReference(STRUCT_OFFSET(UObjectProperty, PropertyClass), TEXT("PropertyClass"));
	}
);
