/*
 * Copyright 2005 Jacek Caban
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

#include <stdarg.h>
#include <stdio.h>

#define COBJMACROS

#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "wingdi.h"
#include "ole2.h"
#include "docobj.h"

#include "mshtml.h"
#include "mshtmhst.h"

#include "wine/debug.h"

#include "mshtml_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(mshtml);

static const WCHAR wszInternetExplorer_Server[] =
    {'I','n','t','e','r','n','e','t',' ','E','x','p','l','o','r','e','r','_','S','e','r','v','e','r',0};
static const WCHAR wszHTML_Document[] =
    {'H','T','M','L',' ','D','o','c','u','m','e','n','t',0};

static ATOM serverwnd_class = 0;

static void paint_disabled(HWND hwnd) {
    HDC hdc;
    PAINTSTRUCT ps;
    HBRUSH brush;
    RECT rect;
    HFONT font;

    font = CreateFontA(25,0,0,0,400,0,0,0,ANSI_CHARSET,0,0,DEFAULT_QUALITY,DEFAULT_PITCH,NULL);
    brush = CreateSolidBrush(RGB(255,255,255));
    GetClientRect(hwnd, &rect);

    hdc = BeginPaint(hwnd, &ps);
    SelectObject(hdc, font);
    SelectObject(hdc, brush);
    Rectangle(hdc, rect.left, rect.top, rect.right, rect.bottom);
    DrawTextA(hdc, "HTML rendering is currently disabled.",-1, &rect,
            DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    EndPaint(hwnd, &ps);

    DeleteObject(font);
    DeleteObject(brush);
}

static LRESULT WINAPI serverwnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if(msg == WM_PAINT)
        paint_disabled(hwnd);
        
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void register_serverwnd_class(void)
{
    static WNDCLASSEXW wndclass = {
        sizeof(WNDCLASSEXW),
        CS_DBLCLKS,
        serverwnd_proc,
        0, 0, NULL, NULL, NULL, NULL, NULL,
        wszInternetExplorer_Server,
        NULL,
    };
    wndclass.hInstance = hInst;
    serverwnd_class = RegisterClassExW(&wndclass);
}


/**********************************************************
 * IOleDocumentView implementation
 */

#define DOCVIEW_THIS(iface) DEFINE_THIS(HTMLDocument, OleDocumentView, iface)

static HRESULT WINAPI OleDocumentView_QueryInterface(IOleDocumentView *iface, REFIID riid, void **ppvObject)
{
    HTMLDocument *This = DOCVIEW_THIS(iface);
    return IHTMLDocument2_QueryInterface(HTMLDOC(This), riid, ppvObject);
}

static ULONG WINAPI OleDocumentView_AddRef(IOleDocumentView *iface)
{
    HTMLDocument *This = DOCVIEW_THIS(iface);
    return IHTMLDocument2_AddRef(HTMLDOC(This));
}

static ULONG WINAPI OleDocumentView_Release(IOleDocumentView *iface)
{
    HTMLDocument *This = DOCVIEW_THIS(iface);
    return IHTMLDocument2_Release(HTMLDOC(This));
}

static HRESULT WINAPI OleDocumentView_SetInPlaceSite(IOleDocumentView *iface, IOleInPlaceSite *pIPSite)
{
    HTMLDocument *This = DOCVIEW_THIS(iface);
    TRACE("(%p)->(%p)\n", This, pIPSite);

    if(pIPSite)
        IOleInPlaceSite_AddRef(pIPSite);

    if(This->ipsite)
        IOleInPlaceSite_Release(This->ipsite);

    This->ipsite = pIPSite;
    return S_OK;
}

static HRESULT WINAPI OleDocumentView_GetInPlaceSite(IOleDocumentView *iface, IOleInPlaceSite **ppIPSite)
{
    HTMLDocument *This = DOCVIEW_THIS(iface);
    TRACE("(%p)->(%p)\n", This, ppIPSite);

    if(!ppIPSite)
        return E_INVALIDARG;

    if(This->ipsite)
        IOleInPlaceSite_AddRef(This->ipsite);

    *ppIPSite = This->ipsite;
    return S_OK;
}

static HRESULT WINAPI OleDocumentView_GetDocument(IOleDocumentView *iface, IUnknown **ppunk)
{
    HTMLDocument *This = DOCVIEW_THIS(iface);
    TRACE("(%p)->(%p)\n", This, ppunk);

    if(!ppunk)
        return E_INVALIDARG;

    IHTMLDocument2_AddRef(HTMLDOC(This));
    *ppunk = (IUnknown*)HTMLDOC(This);
    return S_OK;
}

static HRESULT WINAPI OleDocumentView_SetRect(IOleDocumentView *iface, LPRECT prcView)
{
    HTMLDocument *This = DOCVIEW_THIS(iface);
    RECT rect;

    TRACE("(%p)->(%p)\n", This, prcView);

    if(!prcView)
        return E_INVALIDARG;

    if(This->hwnd) {
        GetClientRect(This->hwnd, &rect);
        if(memcmp(prcView, &rect, sizeof(RECT))) {
            InvalidateRect(This->hwnd,NULL,TRUE);
            SetWindowPos(This->hwnd, NULL, prcView->left, prcView->top, prcView->right,
                    prcView->bottom, SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    
    return S_OK;
}

static HRESULT WINAPI OleDocumentView_GetRect(IOleDocumentView *iface, LPRECT prcView)
{
    HTMLDocument *This = DOCVIEW_THIS(iface);

    TRACE("(%p)->(%p)\n", This, prcView);

    if(!prcView)
        return E_INVALIDARG;

    GetClientRect(This->hwnd, prcView);
    return S_OK;
}

static HRESULT WINAPI OleDocumentView_SetRectComplex(IOleDocumentView *iface, LPRECT prcView,
                        LPRECT prcHScroll, LPRECT prcVScroll, LPRECT prcSizeBox)
{
    HTMLDocument *This = DOCVIEW_THIS(iface);
    FIXME("(%p)->(%p %p %p %p)\n", This, prcView, prcHScroll, prcVScroll, prcSizeBox);
    return E_NOTIMPL;
}

static HRESULT WINAPI OleDocumentView_Show(IOleDocumentView *iface, BOOL fShow)
{
    HTMLDocument *This = DOCVIEW_THIS(iface);
    TRACE("(%p)->(%x)\n", This, fShow);

    if(This->hwnd)
        ShowWindow(This->hwnd, fShow);

    return S_OK;
}

static HRESULT WINAPI OleDocumentView_UIActivate(IOleDocumentView *iface, BOOL fUIActivate)
{
    HTMLDocument *This = DOCVIEW_THIS(iface);
    HRESULT hres;
    IOleInPlaceUIWindow *pIPWnd;
    IOleInPlaceFrame *pIPFrame;
    RECT posrect, cliprect;
    OLEINPLACEFRAMEINFO frameinfo;
    HWND parent_hwnd;

    TRACE("(%p)->(%x)\n", This, fUIActivate);

    if(!This->ipsite) {
        FIXME("This->ipsite = NULL\n");
        return E_FAIL;
    }

    if(fUIActivate) {
        if(This->ui_active)
            return S_OK;
        if(!serverwnd_class)
            register_serverwnd_class();

        hres = IOleInPlaceSite_CanInPlaceActivate(This->ipsite);
        if(hres != S_OK) {
            WARN("CanInPlaceActivate returned: %08lx\n", hres);
            return FAILED(hres) ? hres : E_FAIL;
        }

        hres = IOleInPlaceSite_GetWindowContext(This->ipsite, &pIPFrame, &pIPWnd, &posrect, &cliprect, &frameinfo);
        if(FAILED(hres)) {
            WARN("GetWindowContext failed: %08lx\n", hres);
            return hres;
        }
        if(pIPWnd)
            IOleInPlaceUIWindow_Release(pIPWnd);
        TRACE("got window context: %p %p {%ld %ld %ld %ld} {%ld %ld %ld %ld} {%d %x %p %p %d}\n",
                pIPFrame, pIPWnd, posrect.left, posrect.top, posrect.right, posrect.bottom,
                cliprect.left, cliprect.top, cliprect.right, cliprect.bottom,
                frameinfo.cb, frameinfo.fMDIApp, frameinfo.hwndFrame, frameinfo.haccel, frameinfo.cAccelEntries);

        hres = IOleInPlaceSite_GetWindow(This->ipsite, &parent_hwnd);
        if(FAILED(hres)) {
            WARN("GetWindow failed: %08lx\n", hres);
            return hres;
        }

        TRACE("got parent window %p\n", parent_hwnd);

        if(This->hwnd) {
            if(GetParent(This->hwnd) != parent_hwnd)
                SetParent(This->hwnd, parent_hwnd);
            SetWindowPos(This->hwnd, HWND_TOP,
                    posrect.left, posrect.top, posrect.right-posrect.left, posrect.bottom-posrect.top,
                    SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }else {
            This->hwnd = CreateWindowExW(0, wszInternetExplorer_Server, NULL,
                    WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                    posrect.left, posrect.top, posrect.right-posrect.left, posrect.bottom-posrect.top,
                    parent_hwnd, NULL, hInst, This);
            SetWindowPos(This->hwnd, NULL, 0, 0, 0, 0,
                    SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOREDRAW | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            RedrawWindow(This->hwnd, NULL, NULL, RDW_INVALIDATE | RDW_NOERASE | RDW_ALLCHILDREN);
            SetFocus(This->hwnd);
        }

        This->in_place_active = TRUE;
        hres = IOleInPlaceSite_OnInPlaceActivate(This->ipsite);
        if(FAILED(hres)) {
            WARN("OnInPlaceActivate failed: %08lx\n", hres);
            This->in_place_active = FALSE;
            return hres;
        }

        /* NOTE:
         * Windows implementation calls:
         * RegisterWindowMessage("MSWHEEL_ROLLMSG");
         * SetTimer(This->hwnd, TIMER_ID, 100, NULL);
         */

        This->ui_active = TRUE;
        hres = IOleInPlaceSite_OnUIActivate(This->ipsite);
        if(SUCCEEDED(hres)) {
            IOleInPlaceFrame_SetActiveObject(pIPFrame, ACTOBJ(This), wszHTML_Document);
        }else {
            FIXME("OnUIActivate failed: %08lx\n", hres);
            This->ui_active = FALSE;
            return hres;
        }
        if(This->frame)
            IOleInPlaceFrame_Release(This->frame);
        This->frame = pIPFrame;

        hres = IDocHostUIHandler_ShowUI(This->hostui, 0, ACTOBJ(This), CMDTARGET(This),
                pIPFrame, NULL);
        if(FAILED(hres))
            IDocHostUIHandler_HideUI(This->hostui);
    }else {
        This->ui_active = FALSE;
        if(This->frame)
            IOleInPlaceFrame_SetActiveObject(This->frame, NULL, NULL);
        if(This->hostui)
            IDocHostUIHandler_HideUI(This->hostui);
        if(This->ipsite)
            IOleInPlaceSite_OnUIDeactivate(This->ipsite, FALSE);
    }
    return S_OK;
}

static HRESULT WINAPI OleDocumentView_Open(IOleDocumentView *iface)
{
    HTMLDocument *This = DOCVIEW_THIS(iface);
    FIXME("(%p)\n", This);
    return E_NOTIMPL;
}

static HRESULT WINAPI OleDocumentView_CloseView(IOleDocumentView *iface, DWORD dwReserved)
{
    HTMLDocument *This = DOCVIEW_THIS(iface);
    TRACE("(%p)->(%lx)\n", This, dwReserved);

    if(dwReserved)
        WARN("dwReserved = %ld\n", dwReserved);

    /* NOTE:
     * Windows implementation calls QueryInterface(IID_IOleCommandTarget),
     * QueryInterface(IID_IOleControlSite) and KillTimer
     */

    IOleDocumentView_Show(iface, FALSE);

    return S_OK;
}

static HRESULT WINAPI OleDocumentView_SaveViewState(IOleDocumentView *iface, LPSTREAM pstm)
{
    HTMLDocument *This = DOCVIEW_THIS(iface);
    FIXME("(%p)->(%p)\n", This, pstm);
    return E_NOTIMPL;
}

static HRESULT WINAPI OleDocumentView_ApplyViewState(IOleDocumentView *iface, LPSTREAM pstm)
{
    HTMLDocument *This = DOCVIEW_THIS(iface);
    FIXME("(%p)->(%p)\n", This, pstm);
    return E_NOTIMPL;
}

static HRESULT WINAPI OleDocumentView_Clone(IOleDocumentView *iface, IOleInPlaceSite *pIPSiteNew,
                                        IOleDocumentView **ppViewNew)
{
    HTMLDocument *This = DOCVIEW_THIS(iface);
    FIXME("(%p)->(%p %p)\n", This, pIPSiteNew, ppViewNew);
    return E_NOTIMPL;
}

#undef DOCVIEW_THIS

static const IOleDocumentViewVtbl OleDocumentViewVtbl = {
    OleDocumentView_QueryInterface,
    OleDocumentView_AddRef,
    OleDocumentView_Release,
    OleDocumentView_SetInPlaceSite,
    OleDocumentView_GetInPlaceSite,
    OleDocumentView_GetDocument,
    OleDocumentView_SetRect,
    OleDocumentView_GetRect,
    OleDocumentView_SetRectComplex,
    OleDocumentView_Show,
    OleDocumentView_UIActivate,
    OleDocumentView_Open,
    OleDocumentView_CloseView,
    OleDocumentView_SaveViewState,
    OleDocumentView_ApplyViewState,
    OleDocumentView_Clone
};

/**********************************************************
 * IViewObject implementation
 */

#define VIEWOBJ_THIS(iface) DEFINE_THIS(HTMLDocument, ViewObject2, iface)

static HRESULT WINAPI ViewObject_QueryInterface(IViewObject2 *iface, REFIID riid, void **ppvObject)
{
    HTMLDocument *This = VIEWOBJ_THIS(iface);
    return IHTMLDocument2_QueryInterface(HTMLDOC(This), riid, ppvObject);
}

static ULONG WINAPI ViewObject_AddRef(IViewObject2 *iface)
{
    HTMLDocument *This = VIEWOBJ_THIS(iface);
    return IHTMLDocument2_AddRef(HTMLDOC(This));
}

static ULONG WINAPI ViewObject_Release(IViewObject2 *iface)
{
    HTMLDocument *This = VIEWOBJ_THIS(iface);
    return IHTMLDocument2_Release(HTMLDOC(This));
}

static HRESULT WINAPI ViewObject_Draw(IViewObject2 *iface, DWORD dwDrawAspect, LONG lindex, void *pvAspect,
        DVTARGETDEVICE *ptd, HDC hdcTargetDev, HDC hdcDraw, LPCRECTL lprcBounds,
        LPCRECTL lprcWBounds, BOOL (CALLBACK *pfnContinue)(ULONG_PTR dwContinue), ULONG_PTR dwContinue)
{
    HTMLDocument *This = VIEWOBJ_THIS(iface);
    FIXME("(%p)->(%ld %ld %p %p %p %p %p %p %p %ld)\n", This, dwDrawAspect, lindex, pvAspect,
            ptd, hdcTargetDev, hdcDraw, lprcBounds, lprcWBounds, pfnContinue, dwContinue);
    return E_NOTIMPL;
}

static HRESULT WINAPI ViewObject_GetColorSet(IViewObject2 *iface, DWORD dwDrawAspect, LONG lindex, void *pvAspect,
        DVTARGETDEVICE *ptd, HDC hicTargetDev, LOGPALETTE **ppColorSet)
{
    HTMLDocument *This = VIEWOBJ_THIS(iface);
    FIXME("(%p)->(%ld %ld %p %p %p %p)\n", This, dwDrawAspect, lindex, pvAspect, ptd, hicTargetDev, ppColorSet);
    return E_NOTIMPL;
}

static HRESULT WINAPI ViewObject_Freeze(IViewObject2 *iface, DWORD dwDrawAspect, LONG lindex,
        void *pvAspect, DWORD *pdwFreeze)
{
    HTMLDocument *This = VIEWOBJ_THIS(iface);
    FIXME("(%p)->(%ld %ld %p %p)\n", This, dwDrawAspect, lindex, pvAspect, pdwFreeze);
    return E_NOTIMPL;
}

static HRESULT WINAPI ViewObject_Unfreeze(IViewObject2 *iface, DWORD dwFreeze)
{
    HTMLDocument *This = VIEWOBJ_THIS(iface);
    FIXME("(%p)->(%ld)\n", This, dwFreeze);
    return E_NOTIMPL;
}

static HRESULT WINAPI ViewObject_SetAdvise(IViewObject2 *iface, DWORD aspects, DWORD advf, IAdviseSink *pAdvSink)
{
    HTMLDocument *This = VIEWOBJ_THIS(iface);
    FIXME("(%p)->(%ld %ld %p)\n", This, aspects, advf, pAdvSink);
    return E_NOTIMPL;
}

static HRESULT WINAPI ViewObject_GetAdvise(IViewObject2 *iface, DWORD *pAspects, DWORD *pAdvf, IAdviseSink **ppAdvSink)
{
    HTMLDocument *This = VIEWOBJ_THIS(iface);
    FIXME("(%p)->(%p %p %p)\n", This, pAspects, pAdvf, ppAdvSink);
    return E_NOTIMPL;
}

static HRESULT WINAPI ViewObject_GetExtent(IViewObject2 *iface, DWORD dwDrawAspect, LONG lindex,
                                DVTARGETDEVICE* ptd, LPSIZEL lpsizel)
{
    HTMLDocument *This = VIEWOBJ_THIS(iface);
    FIXME("(%p)->(%ld %ld %p %p)\n", This, dwDrawAspect, lindex, ptd, lpsizel);
    return E_NOTIMPL;
}

#undef VIEWOBJ_THIS

static const IViewObject2Vtbl ViewObjectVtbl = {
    ViewObject_QueryInterface,
    ViewObject_AddRef,
    ViewObject_Release,
    ViewObject_Draw,
    ViewObject_GetColorSet,
    ViewObject_Freeze,
    ViewObject_Unfreeze,
    ViewObject_SetAdvise,
    ViewObject_GetAdvise,
    ViewObject_GetExtent
};

void HTMLDocument_View_Init(HTMLDocument *This)
{
    This->lpOleDocumentViewVtbl = &OleDocumentViewVtbl;
    This->lpViewObject2Vtbl = &ViewObjectVtbl;

    This->ipsite = NULL;
    This->frame = NULL;
    This->hwnd = NULL;

    This->in_place_active = FALSE;
    This->ui_active = FALSE;
}
