// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_CEF3

#if PLATFORM_WINDOWS
	#include "AllowWindowsPlatformTypes.h"
#endif

#include "include/cef_client.h"
#include "include/wrapper/cef_message_router.h"

#if PLATFORM_WINDOWS
	#include "HideWindowsPlatformTypes.h"
#endif


class FWebBrowserWindow;


/**
 * Implements CEF Client and other Browser level interfaces.
 */
class FWebBrowserHandler
	: public CefClient
	, public CefDisplayHandler
	, public CefLifeSpanHandler
	, public CefLoadHandler
	, public CefRenderHandler
	, public CefRequestHandler
{
public:

	/** Default constructor. */
	FWebBrowserHandler();

public:

	/**
	 * Pass in a pointer to our Browser Window so that events can be passed on.
	 *
	 * @param InBrowserWindow The browser window this will be handling.
	 */
	void SetBrowserWindow(TSharedPtr<FWebBrowserWindow> InBrowserWindow);

	/** Sets whether to show messages on loading errors. */
	void SetShowErrorMessage(bool InShowErrorMessage)
	{
		ShowErrorMessage = InShowErrorMessage;
	}

public:

	// CefClient Interface

	virtual CefRefPtr<CefDisplayHandler> GetDisplayHandler() override
	{
		return this;
	}

	virtual CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override
	{
		return this;
	}

	virtual CefRefPtr<CefLoadHandler> GetLoadHandler() override
	{
		return this;
	}

	virtual CefRefPtr<CefRenderHandler> GetRenderHandler() override
	{
		return this;
	}

	virtual CefRefPtr<CefRequestHandler> GetRequestHandler() override
	{
		return this;
	}

    virtual bool OnProcessMessageReceived(CefRefPtr<CefBrowser> Browser,
        CefProcessId SourceProcess,
        CefRefPtr<CefProcessMessage> Message) override;


public:

	// CefDisplayHandler Interface

	virtual void OnTitleChange(CefRefPtr<CefBrowser> Browser, const CefString& Title) override;
	virtual void OnAddressChange(CefRefPtr<CefBrowser> Browser, CefRefPtr<CefFrame> Frame, const CefString& Url) override;


public:

	// CefLifeSpanHandler Interface

	virtual void OnAfterCreated(CefRefPtr<CefBrowser> Browser) override;
	virtual void OnBeforeClose(CefRefPtr<CefBrowser> Browser) override;

public:

	// CefLoadHandler Interface

	virtual void OnLoadError(CefRefPtr<CefBrowser> Browser,
		CefRefPtr<CefFrame> Frame,
		CefLoadHandler::ErrorCode InErrorCode,
		const CefString& ErrorText,
		const CefString& FailedUrl) override;

	virtual void OnLoadingStateChange(
		CefRefPtr<CefBrowser> browser,
		bool isLoading,
		bool canGoBack,
		bool canGoForward) override;

public:

	// CefRenderHandler Interface

	virtual bool GetViewRect(CefRefPtr<CefBrowser> Browser, CefRect& Rect) override;
	virtual void OnPaint(CefRefPtr<CefBrowser> Browser,
		PaintElementType Type,
		const RectList& DirtyRects,
		const void* Buffer,
		int Width, int Height) override;
	virtual void OnCursorChange(CefRefPtr<CefBrowser> Browser, CefCursorHandle Cursor) override;

public:

	// CefRequestHandler Interface

	virtual bool OnBeforeResourceLoad(CefRefPtr<CefBrowser> Browser,
		CefRefPtr<CefFrame> Frame,
		CefRefPtr<CefRequest> Request) override;
    virtual void OnRenderProcessTerminated(CefRefPtr<CefBrowser> Browser, TerminationStatus Status) override;
    virtual bool OnBeforeBrowse(CefRefPtr<CefBrowser> Browser,
        CefRefPtr<CefFrame> Frame,
        CefRefPtr<CefRequest> Request,
        bool IsRedirect) override;

private:

	/** Weak Pointer to our Web Browser window so that events can be passed on while it's valid*/
	TWeakPtr<FWebBrowserWindow> BrowserWindowPtr;

	/** Whether to show an error message in case of loading errors. */
	bool ShowErrorMessage;

    /** The message router is used as a part of a generic message api between Javascript in the render process and the application process */
    CefRefPtr<CefMessageRouterBrowserSide> MessageRouter;

	// Include the default reference counting implementation.
	IMPLEMENT_REFCOUNTING(FWebBrowserHandler);
};


#endif
