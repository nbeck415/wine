/*
 * Implements IBaseFilter. (internal)
 *
 * Copyright (C) Hidenori TAKESHIMA <hidenori@a2.ctktv.ne.jp>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winuser.h"
#include "winerror.h"
#include "strmif.h"
#include "vfwmsgs.h"

#include "wine/debug.h"
WINE_DEFAULT_DEBUG_CHANNEL(quartz);

#include "quartz_private.h"
#include "basefilt.h"
#include "enumunk.h"


/***************************************************************************
 *
 *	CBaseFilterImpl::IBaseFilter
 *
 */

static HRESULT WINAPI
CBaseFilterImpl_fnQueryInterface(IBaseFilter* iface,REFIID riid,void** ppobj)
{
	ICOM_THIS(CBaseFilterImpl,iface);

	TRACE("(%p)->()\n",This);

	return IUnknown_QueryInterface(This->punkControl,riid,ppobj);
}

static ULONG WINAPI
CBaseFilterImpl_fnAddRef(IBaseFilter* iface)
{
	ICOM_THIS(CBaseFilterImpl,iface);

	TRACE("(%p)->()\n",This);

	return IUnknown_AddRef(This->punkControl);
}

static ULONG WINAPI
CBaseFilterImpl_fnRelease(IBaseFilter* iface)
{
	ICOM_THIS(CBaseFilterImpl,iface);

	TRACE("(%p)->()\n",This);

	return IUnknown_Release(This->punkControl);
}


static HRESULT WINAPI
CBaseFilterImpl_fnGetClassID(IBaseFilter* iface,CLSID* pclsid)
{
	ICOM_THIS(CBaseFilterImpl,iface);

	TRACE("(%p)->()\n",This);

	if ( pclsid == NULL )
		return E_POINTER;

	memcpy( pclsid, This->pclsidFilter, sizeof(CLSID) );

	return NOERROR;
}

static HRESULT WINAPI
CBaseFilterImpl_fnStop(IBaseFilter* iface)
{
	ICOM_THIS(CBaseFilterImpl,iface);
	HRESULT hr;

	TRACE("(%p)->()\n",This);

	hr = NOERROR;

	EnterCriticalSection( &This->csFilter );
	TRACE("(%p) state = %d\n",This,This->fstate);

	if ( This->fstate == State_Running )
	{
		if ( This->pHandlers->pOnInactive != NULL )
			hr = This->pHandlers->pOnInactive( This );
		if ( SUCCEEDED(hr) )
			This->fstate = State_Paused;
	}
	if ( This->fstate == State_Paused )
	{
		if ( This->pHandlers->pOnStop != NULL )
			hr = This->pHandlers->pOnStop( This );
		if ( SUCCEEDED(hr) )
			This->fstate = State_Stopped;
	}

	LeaveCriticalSection( &This->csFilter );

	return hr;
}

static HRESULT WINAPI
CBaseFilterImpl_fnPause(IBaseFilter* iface)
{
	ICOM_THIS(CBaseFilterImpl,iface);
	HRESULT hr;

	TRACE("(%p)->()\n",This);

	hr = NOERROR;

	EnterCriticalSection( &This->csFilter );
	TRACE("(%p) state = %d\n",This,This->fstate);

	if ( This->fstate != State_Paused )
	{
		if ( This->pHandlers->pOnInactive != NULL )
			hr = This->pHandlers->pOnInactive( This );
		if ( SUCCEEDED(hr) )
			This->fstate = State_Paused;
	}
	LeaveCriticalSection( &This->csFilter );

	TRACE("hr = %08lx\n",hr);

	return hr;
}

static HRESULT WINAPI
CBaseFilterImpl_fnRun(IBaseFilter* iface,REFERENCE_TIME rtStart)
{
	ICOM_THIS(CBaseFilterImpl,iface);
	HRESULT hr;

	TRACE("(%p)->()\n",This);

	hr = NOERROR;

	EnterCriticalSection( &This->csFilter );
	TRACE("(%p) state = %d\n",This,This->fstate);

	This->rtStart = rtStart;

	if ( This->fstate == State_Stopped )
	{
		if ( This->pHandlers->pOnInactive != NULL )
			hr = This->pHandlers->pOnInactive( This );
		if ( SUCCEEDED(hr) )
			This->fstate = State_Paused;
	}
	if ( This->fstate == State_Paused )
	{
		if ( This->pHandlers->pOnActive != NULL )
			hr = This->pHandlers->pOnActive( This );
		if ( SUCCEEDED(hr) )
			This->fstate = State_Running;
	}

	LeaveCriticalSection( &This->csFilter );

	return hr;
}

static HRESULT WINAPI
CBaseFilterImpl_fnGetState(IBaseFilter* iface,DWORD dw,FILTER_STATE* pState)
{
	ICOM_THIS(CBaseFilterImpl,iface);
	HRESULT hr = S_OK;

	TRACE("(%p)->(%p)\n",This,pState);

	if ( pState == NULL )
		return E_POINTER;

	EnterCriticalSection( &This->csFilter );
	TRACE("(%p) state = %d\n",This,This->fstate);
	*pState = This->fstate;
	if ( This->bIntermediateState )
		hr = VFW_S_STATE_INTERMEDIATE;
	LeaveCriticalSection( &This->csFilter );

	return hr;
}

static HRESULT WINAPI
CBaseFilterImpl_fnSetSyncSource(IBaseFilter* iface,IReferenceClock* pobjClock)
{
	ICOM_THIS(CBaseFilterImpl,iface);

	TRACE("(%p)->(%p)\n",This,pobjClock);

	EnterCriticalSection( &This->csFilter );

	if ( This->pClock != NULL )
	{
		IReferenceClock_Release( This->pClock );
		This->pClock = NULL;
	}

	This->pClock = pobjClock;
	if ( pobjClock != NULL )
		IReferenceClock_AddRef( pobjClock );

	LeaveCriticalSection( &This->csFilter );

	return NOERROR;
}

static HRESULT WINAPI
CBaseFilterImpl_fnGetSyncSource(IBaseFilter* iface,IReferenceClock** ppobjClock)
{
	ICOM_THIS(CBaseFilterImpl,iface);
	HRESULT hr = VFW_E_NO_CLOCK;

	TRACE("(%p)->(%p)\n",This,ppobjClock);

	if ( ppobjClock == NULL )
		return E_POINTER;

	EnterCriticalSection( &This->csFilter );

	*ppobjClock = This->pClock;
	if ( This->pClock != NULL )
	{
		hr = NOERROR;
		IReferenceClock_AddRef( This->pClock );
	}

	LeaveCriticalSection( &This->csFilter );

	return hr;
}


static HRESULT WINAPI
CBaseFilterImpl_fnEnumPins(IBaseFilter* iface,IEnumPins** ppenum)
{
	ICOM_THIS(CBaseFilterImpl,iface);
	HRESULT	hr = E_FAIL;
	QUARTZ_CompList*	pListPins;
	QUARTZ_CompListItem*	pItem;
	IUnknown*	punkPin;

	TRACE("(%p)->(%p)\n",This,ppenum);

	if ( ppenum == NULL )
		return E_POINTER;
	*ppenum = NULL;

	pListPins = QUARTZ_CompList_Alloc();
	if ( pListPins == NULL )
		return E_OUTOFMEMORY;

	QUARTZ_CompList_Lock( This->pInPins );
	QUARTZ_CompList_Lock( This->pOutPins );

	pItem = QUARTZ_CompList_GetFirst( This->pInPins );
	while ( pItem != NULL )
	{
		punkPin = QUARTZ_CompList_GetItemPtr( pItem );
		hr = QUARTZ_CompList_AddComp( pListPins, punkPin, NULL, 0 );
		if ( FAILED(hr) )
			goto err;
		pItem = QUARTZ_CompList_GetNext( This->pInPins, pItem );
	}

	pItem = QUARTZ_CompList_GetFirst( This->pOutPins );
	while ( pItem != NULL )
	{
		punkPin = QUARTZ_CompList_GetItemPtr( pItem );
		hr = QUARTZ_CompList_AddComp( pListPins, punkPin, NULL, 0 );
		if ( FAILED(hr) )
			goto err;
		pItem = QUARTZ_CompList_GetNext( This->pOutPins, pItem );
	}

	hr = QUARTZ_CreateEnumUnknown(
		&IID_IEnumPins, (void**)ppenum, pListPins );
err:
	QUARTZ_CompList_Unlock( This->pInPins );
	QUARTZ_CompList_Unlock( This->pOutPins );

	QUARTZ_CompList_Free( pListPins );

	return hr;
}

static HRESULT WINAPI
CBaseFilterImpl_fnFindPin(IBaseFilter* iface,LPCWSTR lpwszId,IPin** ppobj)
{
	ICOM_THIS(CBaseFilterImpl,iface);

	FIXME("(%p)->(%s,%p) stub!\n",This,debugstr_w(lpwszId),ppobj);

	if ( ppobj == NULL )
		return E_POINTER;



	return E_NOTIMPL;
}

static HRESULT WINAPI
CBaseFilterImpl_fnQueryFilterInfo(IBaseFilter* iface,FILTER_INFO* pfi)
{
	ICOM_THIS(CBaseFilterImpl,iface);

	TRACE("(%p)->(%p)\n",This,pfi);

	if ( pfi == NULL )
		return E_POINTER;

	EnterCriticalSection( &This->csFilter );

	if ( This->cbNameGraph <= sizeof(WCHAR)*MAX_FILTER_NAME )
	{
		memcpy( pfi->achName, This->pwszNameGraph, This->cbNameGraph );
	}
	else
	{
		memcpy( pfi->achName, This->pwszNameGraph,
				sizeof(WCHAR)*MAX_FILTER_NAME );
		pfi->achName[MAX_FILTER_NAME-1] = (WCHAR)0;
	}

	pfi->pGraph = This->pfg;
	if ( pfi->pGraph != NULL )
		IFilterGraph_AddRef(pfi->pGraph);

	LeaveCriticalSection( &This->csFilter );

	return NOERROR;
}

static HRESULT WINAPI
CBaseFilterImpl_fnJoinFilterGraph(IBaseFilter* iface,IFilterGraph* pfg,LPCWSTR lpwszName)
{
	ICOM_THIS(CBaseFilterImpl,iface);
	HRESULT	hr;

	TRACE("(%p)->(%p,%s)\n",This,pfg,debugstr_w(lpwszName));

	EnterCriticalSection( &This->csFilter );

	if ( This->pwszNameGraph != NULL )
	{
		QUARTZ_FreeMem( This->pwszNameGraph );
		This->pwszNameGraph = NULL;
		This->cbNameGraph = 0;
	}

	This->pfg = pfg;
	This->cbNameGraph = sizeof(WCHAR) * (lstrlenW(lpwszName)+1);
	This->pwszNameGraph = (WCHAR*)QUARTZ_AllocMem( This->cbNameGraph );
	if ( This->pwszNameGraph == NULL )
	{
		hr = E_OUTOFMEMORY;
		goto err;
	}
	memcpy( This->pwszNameGraph, lpwszName, This->cbNameGraph );

	hr = NOERROR;
err:
	LeaveCriticalSection( &This->csFilter );

	return hr;
}

static HRESULT WINAPI
CBaseFilterImpl_fnQueryVendorInfo(IBaseFilter* iface,LPWSTR* lpwszVendor)
{
	ICOM_THIS(CBaseFilterImpl,iface);

	TRACE("(%p)->(%p)\n",This,lpwszVendor);

	/* E_NOTIMPL means 'no vender information'. */
	return E_NOTIMPL;
}


/***************************************************************************
 *
 *	construct/destruct CBaseFilterImpl
 *
 */

static ICOM_VTABLE(IBaseFilter) ibasefilter =
{
	ICOM_MSVTABLE_COMPAT_DummyRTTIVALUE
	/* IUnknown fields */
	CBaseFilterImpl_fnQueryInterface,
	CBaseFilterImpl_fnAddRef,
	CBaseFilterImpl_fnRelease,
	/* IPersist fields */
	CBaseFilterImpl_fnGetClassID,
	/* IMediaFilter fields */
	CBaseFilterImpl_fnStop,
	CBaseFilterImpl_fnPause,
	CBaseFilterImpl_fnRun,
	CBaseFilterImpl_fnGetState,
	CBaseFilterImpl_fnSetSyncSource,
	CBaseFilterImpl_fnGetSyncSource,
	/* IBaseFilter fields */
	CBaseFilterImpl_fnEnumPins,
	CBaseFilterImpl_fnFindPin,
	CBaseFilterImpl_fnQueryFilterInfo,
	CBaseFilterImpl_fnJoinFilterGraph,
	CBaseFilterImpl_fnQueryVendorInfo,
};


HRESULT CBaseFilterImpl_InitIBaseFilter(
	CBaseFilterImpl* This, IUnknown* punkControl,
	const CLSID* pclsidFilter, LPCWSTR lpwszNameGraph,
	const CBaseFilterHandlers* pHandlers )
{
	TRACE("(%p,%p)\n",This,punkControl);

	if ( punkControl == NULL )
	{
		ERR( "punkControl must not be NULL\n" );
		return E_INVALIDARG;
	}

	ICOM_VTBL(This) = &ibasefilter;
	This->punkControl = punkControl;
	This->pHandlers = pHandlers;
	This->pclsidFilter = pclsidFilter;
	This->pInPins = NULL;
	This->pOutPins = NULL;
	This->pfg = NULL;
	This->cbNameGraph = 0;
	This->pwszNameGraph = NULL;
	This->pClock = NULL;
	This->rtStart = 0;
	This->fstate = State_Stopped;
	This->bIntermediateState = FALSE;

	This->cbNameGraph = sizeof(WCHAR) * (lstrlenW(lpwszNameGraph)+1);
	This->pwszNameGraph = (WCHAR*)QUARTZ_AllocMem( This->cbNameGraph );
	if ( This->pwszNameGraph == NULL )
		return E_OUTOFMEMORY;
	memcpy( This->pwszNameGraph, lpwszNameGraph, This->cbNameGraph );

	This->pInPins = QUARTZ_CompList_Alloc();
	This->pOutPins = QUARTZ_CompList_Alloc();
	if ( This->pInPins == NULL || This->pOutPins == NULL )
	{
		if ( This->pInPins != NULL )
			QUARTZ_CompList_Free(This->pInPins);
		if ( This->pOutPins != NULL )
			QUARTZ_CompList_Free(This->pOutPins);
		QUARTZ_FreeMem(This->pwszNameGraph);
		return E_OUTOFMEMORY;
	}

	InitializeCriticalSection( &This->csFilter );

	return NOERROR;
}

void CBaseFilterImpl_UninitIBaseFilter( CBaseFilterImpl* This )
{
	QUARTZ_CompListItem*	pListItem;
	IPin*	pPin;

	TRACE("(%p)\n",This);

	if ( This->pInPins != NULL )
	{
		while ( 1 )
		{
			pListItem = QUARTZ_CompList_GetFirst( This->pInPins );
			if ( pListItem == NULL )
				break;
			pPin = (IPin*)QUARTZ_CompList_GetItemPtr( pListItem );
			QUARTZ_CompList_RemoveComp( This->pInPins, (IUnknown*)pPin );
		}

		QUARTZ_CompList_Free( This->pInPins );
		This->pInPins = NULL;
	}
	if ( This->pOutPins != NULL )
	{
		while ( 1 )
		{
			pListItem = QUARTZ_CompList_GetFirst( This->pOutPins );
			if ( pListItem == NULL )
				break;
			pPin = (IPin*)QUARTZ_CompList_GetItemPtr( pListItem );
			QUARTZ_CompList_RemoveComp( This->pOutPins, (IUnknown*)pPin );
		}

		QUARTZ_CompList_Free( This->pOutPins );
		This->pOutPins = NULL;
	}

	if ( This->pwszNameGraph != NULL )
	{
		QUARTZ_FreeMem( This->pwszNameGraph );
		This->pwszNameGraph = NULL;
	}

	if ( This->pClock != NULL )
	{
		IReferenceClock_Release( This->pClock );
		This->pClock = NULL;
	}

	DeleteCriticalSection( &This->csFilter );
}

/***************************************************************************
 *
 *	CBaseFilterImpl methods
 *
 */

HRESULT CBaseFilterImpl_MediaEventNotify(
	CBaseFilterImpl* This, long lEvent,LONG_PTR lParam1,LONG_PTR lParam2)
{
	IMediaEventSink*	pSink = NULL;
	HRESULT	hr = E_NOTIMPL;

	EnterCriticalSection( &This->csFilter );

	if ( This->pfg == NULL )
	{
		hr = E_UNEXPECTED;
		goto err;
	}

	hr = IFilterGraph_QueryInterface( This->pfg, &IID_IMediaEventSink, (void**)&pSink );
	if ( FAILED(hr) )
		goto err;
	if ( pSink == NULL )
	{
		hr = E_FAIL;
		goto err;
	}

	hr = IMediaEventSink_Notify(pSink,lEvent,lParam1,lParam2);
	IMediaEventSink_Release(pSink);
err:
	LeaveCriticalSection( &This->csFilter );

	return hr;
}

