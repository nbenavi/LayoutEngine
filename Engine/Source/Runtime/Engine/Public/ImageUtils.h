// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
ImageUtils.h: Image utility functions.
=============================================================================*/

#ifndef __IMAGEUTILS_H__
#define __IMAGEUTILS_H__

/**
 *	Parameters used for creating a Texture2D frmo a simple color buffer.
 */
struct FCreateTexture2DParameters
{
	/** True if alpha channel is used */
	bool						bUseAlpha;

	/** Compression settings to use for texture */
	TextureCompressionSettings	CompressionSettings;

	/** If texture should be compressed right away, or defer until package is saved */
	bool						bDeferCompression;

	/** If texture should be set as SRGB */
	bool						bSRGB;

	/* The Guid hash to use part of the texture source's DDC key */
	FGuid						SourceGuidHash;

	FCreateTexture2DParameters()
		:	bUseAlpha(false),
			CompressionSettings(TC_Default),
			bDeferCompression(false),
			bSRGB(true)
	{
	}
};

/**
 * Class of static image utility functions.
 */
class FImageUtils
{
public:
	/**
	 * Resizes the given image using a simple average filter and stores it in the destination array.  This version constrains aspect ratio.
	 *
	 * @param SrcWidth	Source image width.
	 * @param SrcHeight	Source image height.
	 * @param SrcData	Source image data.
	 * @param DstWidth	Destination image width.
	 * @param DstHeight Destination image height.
	 * @param DstData	Destination image data.
	 */
	ENGINE_API static void ImageResize(int32 SrcWidth, int32 SrcHeight, const TArray<FColor> &SrcData,  int32 DstWidth, int32 DstHeight, TArray<FColor> &DstData, bool bLinearSpace );

	/**
	 * Creates a 2D texture from a array of raw color data.
	 *
	 * @param SrcWidth		Source image width.
	 * @param SrcHeight		Source image height.
	 * @param SrcData		Source image data.
	 * @param Outer			Outer for the texture object.
	 * @param Name			Name for the texture object.
	 * @param Flags			Object flags for the texture object.
	 * @param InParams		Params about how to set up the texture.
	 * @return				Returns a pointer to the constructed 2D texture object.
	 *
	 */
	ENGINE_API static UTexture2D* CreateTexture2D(int32 SrcWidth, int32 SrcHeight, const TArray<FColor> &SrcData, UObject* Outer, const FString& Name, const EObjectFlags &Flags, const FCreateTexture2DParameters& InParams);

	/**
	 * Crops, and scales an image from a raw image array.
	 *
	 * @param SrcWidth			Source image width.
	 * @param SrcHeight			Source image height.
	 * @param DesiredWidth		Desired Width.
	 * @param DesiredHeight		Desired Height.
	 * @param SrcData			Raw image array.
	 * @param DstData			compressed image array.
	 *
	 */
	ENGINE_API static void CropAndScaleImage( int32 SrcWidth, int32 SrcHeight, int32 DesiredWidth, int32 DesiredHeight, const TArray<FColor> &SrcData, TArray<FColor> &DstData  );

	/**
	 * Compress image to .png uint8 array.
	 *
	 * @param ImageHeight		Source image width.
	 * @param ImageWidth		Source image height.
	 * @param SrcData			Raw image array.
	 * @param DstData			compressed image array.
	 *
	 */
	ENGINE_API static void CompressImageArray( int32 ImageWidth, int32 ImageHeight, TArray<FColor> &SrcData, TArray<uint8> &DstData );
};

#endif

