/*
 * Primary DirectDraw video stream
 *
 * Copyright 2005, 2008, 2012 Christian Costa
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#define COBJMACROS
#include "amstream_private.h"
#include "wine/debug.h"
#include "wine/list.h"
#include "wine/strmbase.h"

WINE_DEFAULT_DEBUG_CHANNEL(quartz);

static const WCHAR sink_id[] = L"I{A35FF56A-9FDA-11D0-8FDF-00C04FD9189D}";

struct format
{
    DWORD flags;
    DWORD width;
    DWORD height;
    DDPIXELFORMAT pf;
};

struct ddraw_stream
{
    IAMMediaStream IAMMediaStream_iface;
    IDirectDrawMediaStream IDirectDrawMediaStream_iface;
    IMemInputPin IMemInputPin_iface;
    IMemAllocator IMemAllocator_iface;
    IPin IPin_iface;
    LONG ref;
    LONG sample_refs;

    IMultiMediaStream* parent;
    MSPID purpose_id;
    STREAM_TYPE stream_type;
    IDirectDraw *ddraw;
    CRITICAL_SECTION cs;
    IMediaStreamFilter *filter;
    IFilterGraph *graph;

    IPin *peer;
    BOOL using_private_allocator;
    AM_MEDIA_TYPE mt;
    struct format format;
    FILTER_STATE state;
    REFERENCE_TIME segment_start;
    BOOL eos;
    BOOL flushing;
    CONDITION_VARIABLE update_queued_cv;
    struct list update_queue;

    CONDITION_VARIABLE allocator_cv;
    bool committed;
    LONG buffer_count; /* Only used for properties. */
};

struct ddraw_sample
{
    IDirectDrawStreamSample IDirectDrawStreamSample_iface;
    LONG ref;
    struct ddraw_stream *parent;
    IMultiMediaStream *mmstream;
    IDirectDrawSurface *surface;
    DDSURFACEDESC surface_desc;
    RECT rect;
    STREAM_TIME start_time;
    STREAM_TIME end_time;
    BOOL continuous_update;
    CONDITION_VARIABLE update_cv;
    HANDLE external_event;

    IMediaSample IMediaSample_iface;
    unsigned int media_sample_refcount;
    bool sync_point, preroll, discontinuity;

    struct list entry;
    HRESULT update_hr;
    bool pending;
};

static HRESULT ddrawstreamsample_create(struct ddraw_stream *parent, IDirectDrawSurface *surface,
    const RECT *rect, IDirectDrawStreamSample **ddraw_stream_sample);

static void remove_queued_update(struct ddraw_sample *sample)
{
    sample->pending = false;
    list_remove(&sample->entry);
    WakeConditionVariable(&sample->update_cv);
    if (sample->external_event)
        SetEvent(sample->external_event);
}

static void flush_update_queue(struct ddraw_stream *stream, HRESULT update_hr)
{
    struct list *entry;
    while ((entry = list_head(&stream->update_queue)))
    {
        struct ddraw_sample *sample = LIST_ENTRY(entry, struct ddraw_sample, entry);
        sample->update_hr = update_hr;
        remove_queued_update(sample);
    }
}

static HRESULT copy_sample(struct ddraw_sample *sample, int stride, BYTE *pointer,
        STREAM_TIME start_time, STREAM_TIME end_time)
{
    DDSURFACEDESC desc;
    DWORD row_size;
    const BYTE *src_row;
    BYTE *dst_row;
    DWORD row;
    HRESULT hr;

    desc.dwSize = sizeof(desc);
    hr = IDirectDrawSurface_Lock(sample->surface, &sample->rect, &desc, DDLOCK_WAIT, NULL);
    if (FAILED(hr))
        return hr;

    row_size = (sample->rect.right - sample->rect.left) * desc.ddpfPixelFormat.dwRGBBitCount / 8;
    src_row = pointer;
    dst_row = desc.lpSurface;
    for (row = sample->rect.top; row < sample->rect.bottom; ++row)
    {
        memcpy(dst_row, src_row, row_size);
        src_row += stride;
        dst_row += desc.lPitch;
    }

    hr = IDirectDrawSurface_Unlock(sample->surface, desc.lpSurface);
    if (FAILED(hr))
        return hr;

    sample->start_time = start_time;
    sample->end_time = end_time;

    return S_OK;
}

static BOOL is_format_compatible(struct ddraw_stream *stream,
        DWORD width, DWORD height, const DDPIXELFORMAT *connection_pf)
{
    if (stream->format.flags & DDSD_HEIGHT)
    {
        if (stream->format.width != width || stream->format.height != height)
            return FALSE;
    }
    if (stream->format.flags & DDSD_PIXELFORMAT)
    {
        if (stream->format.pf.dwFlags & DDPF_FOURCC)
            return FALSE;
        if (stream->format.pf.dwRGBBitCount != connection_pf->dwRGBBitCount)
            return FALSE;
        if (stream->format.pf.dwRGBBitCount == 16 && stream->format.pf.dwGBitMask != connection_pf->dwGBitMask)
            return FALSE;
    }
    return TRUE;
}

static inline struct ddraw_stream *impl_from_IAMMediaStream(IAMMediaStream *iface)
{
    return CONTAINING_RECORD(iface, struct ddraw_stream, IAMMediaStream_iface);
}

/*** IUnknown methods ***/
static HRESULT WINAPI ddraw_IAMMediaStream_QueryInterface(IAMMediaStream *iface,
        REFIID riid, void **ret_iface)
{
    struct ddraw_stream *This = impl_from_IAMMediaStream(iface);

    TRACE("(%p/%p)->(%s,%p)\n", iface, This, debugstr_guid(riid), ret_iface);

    if (IsEqualGUID(riid, &IID_IUnknown) ||
        IsEqualGUID(riid, &IID_IMediaStream) ||
        IsEqualGUID(riid, &IID_IAMMediaStream))
    {
        IAMMediaStream_AddRef(iface);
        *ret_iface = iface;
        return S_OK;
    }
    else if (IsEqualGUID(riid, &IID_IDirectDrawMediaStream))
    {
        IAMMediaStream_AddRef(iface);
        *ret_iface = &This->IDirectDrawMediaStream_iface;
        return S_OK;
    }
    else if (IsEqualGUID(riid, &IID_IPin))
    {
        IAMMediaStream_AddRef(iface);
        *ret_iface = &This->IPin_iface;
        return S_OK;
    }
    else if (IsEqualGUID(riid, &IID_IMemInputPin))
    {
        IAMMediaStream_AddRef(iface);
        *ret_iface = &This->IMemInputPin_iface;
        return S_OK;
    }
    else if (IsEqualGUID(riid, &IID_IMemAllocator))
    {
        IAMMediaStream_AddRef(iface);
        *ret_iface = &This->IMemAllocator_iface;
        return S_OK;
    }

    ERR("(%p)->(%s,%p),not found\n", This, debugstr_guid(riid), ret_iface);
    return E_NOINTERFACE;
}

static ULONG WINAPI ddraw_IAMMediaStream_AddRef(IAMMediaStream *iface)
{
    struct ddraw_stream *This = impl_from_IAMMediaStream(iface);
    ULONG ref = InterlockedIncrement(&This->ref);

    TRACE("(%p/%p)->(): new ref = %lu\n", iface, This, ref);

    return ref;
}

static ULONG WINAPI ddraw_IAMMediaStream_Release(IAMMediaStream *iface)
{
    struct ddraw_stream *stream = impl_from_IAMMediaStream(iface);
    ULONG ref = InterlockedDecrement(&stream->ref);

    TRACE("%p decreasing refcount to %lu.\n", stream, ref);

    if (!ref)
    {
        DeleteCriticalSection(&stream->cs);
        if (stream->ddraw)
            IDirectDraw_Release(stream->ddraw);
        free(stream);
    }

    return ref;
}

/*** IMediaStream methods ***/
static HRESULT WINAPI ddraw_IAMMediaStream_GetMultiMediaStream(IAMMediaStream *iface,
        IMultiMediaStream **mmstream)
{
    struct ddraw_stream *stream = impl_from_IAMMediaStream(iface);

    TRACE("stream %p, mmstream %p.\n", stream, mmstream);

    if (!mmstream)
        return E_POINTER;

    if (stream->parent)
        IMultiMediaStream_AddRef(stream->parent);
    *mmstream = stream->parent;
    return S_OK;
}

static HRESULT WINAPI ddraw_IAMMediaStream_GetInformation(IAMMediaStream *iface,
        MSPID *purpose_id, STREAM_TYPE *type)
{
    struct ddraw_stream *This = impl_from_IAMMediaStream(iface);

    TRACE("(%p/%p)->(%p,%p)\n", This, iface, purpose_id, type);

    if (purpose_id)
        *purpose_id = This->purpose_id;
    if (type)
        *type = This->stream_type;

    return S_OK;
}

static HRESULT WINAPI ddraw_IAMMediaStream_SetSameFormat(IAMMediaStream *iface,
        IMediaStream *pStreamThatHasDesiredFormat, DWORD flags)
{
    struct ddraw_stream *This = impl_from_IAMMediaStream(iface);

    FIXME("(%p/%p)->(%p,%lx) stub!\n", This, iface, pStreamThatHasDesiredFormat, flags);

    return S_FALSE;
}

static HRESULT WINAPI ddraw_IAMMediaStream_AllocateSample(IAMMediaStream *iface,
        DWORD flags, IStreamSample **sample)
{
    struct ddraw_stream *This = impl_from_IAMMediaStream(iface);

    FIXME("(%p/%p)->(%lx,%p) stub!\n", This, iface, flags, sample);

    return S_FALSE;
}

static HRESULT WINAPI ddraw_IAMMediaStream_CreateSharedSample(IAMMediaStream *iface,
        IStreamSample *existing_sample, DWORD flags, IStreamSample **sample)
{
    struct ddraw_stream *This = impl_from_IAMMediaStream(iface);

    FIXME("(%p/%p)->(%p,%lx,%p) stub!\n", This, iface, existing_sample, flags, sample);

    return S_FALSE;
}

static HRESULT WINAPI ddraw_IAMMediaStream_SendEndOfStream(IAMMediaStream *iface, DWORD flags)
{
    struct ddraw_stream *This = impl_from_IAMMediaStream(iface);

    FIXME("(%p/%p)->(%lx) stub!\n", This, iface, flags);

    return S_FALSE;
}

/*** IAMMediaStream methods ***/
static HRESULT WINAPI ddraw_IAMMediaStream_Initialize(IAMMediaStream *iface, IUnknown *source_object, DWORD flags,
                                                    REFMSPID purpose_id, const STREAM_TYPE stream_type)
{
    struct ddraw_stream *stream = impl_from_IAMMediaStream(iface);
    HRESULT hr;

    TRACE("stream %p, source_object %p, flags %lx, purpose_id %s, stream_type %u.\n", stream, source_object, flags,
            debugstr_guid(purpose_id), stream_type);

    if (!purpose_id)
        return E_POINTER;

    if (flags & AMMSF_CREATEPEER)
        FIXME("AMMSF_CREATEPEER is not yet supported.\n");

    stream->purpose_id = *purpose_id;
    stream->stream_type = stream_type;

    if (stream->ddraw)
        IDirectDraw_Release(stream->ddraw);
    stream->ddraw = NULL;

    if (source_object
            && FAILED(hr = IUnknown_QueryInterface(source_object, &IID_IDirectDraw, (void **)&stream->ddraw)))
        FIXME("Stream object doesn't implement IDirectDraw interface, hr %#lx.\n", hr);

    if (!source_object)
    {
        if (FAILED(hr = DirectDrawCreate(NULL, &stream->ddraw, NULL)))
            return hr;
        IDirectDraw_SetCooperativeLevel(stream->ddraw, NULL, DDSCL_NORMAL);
    }

    return S_OK;
}

static HRESULT WINAPI ddraw_IAMMediaStream_SetState(IAMMediaStream *iface, FILTER_STATE state)
{
    struct ddraw_stream *stream = impl_from_IAMMediaStream(iface);

    TRACE("stream %p, state %u.\n", stream, state);

    EnterCriticalSection(&stream->cs);

    if (state == State_Stopped)
        WakeConditionVariable(&stream->update_queued_cv);
    if (stream->state == State_Stopped)
        stream->eos = FALSE;

    stream->state = state;

    LeaveCriticalSection(&stream->cs);

    return S_OK;
}

static HRESULT WINAPI ddraw_IAMMediaStream_JoinAMMultiMediaStream(IAMMediaStream *iface, IAMMultiMediaStream *mmstream)
{
    struct ddraw_stream *stream = impl_from_IAMMediaStream(iface);

    TRACE("stream %p, mmstream %p.\n", stream, mmstream);

    stream->parent = (IMultiMediaStream *)mmstream;

    return S_OK;
}

static HRESULT WINAPI ddraw_IAMMediaStream_JoinFilter(IAMMediaStream *iface, IMediaStreamFilter *filter)
{
    struct ddraw_stream *stream = impl_from_IAMMediaStream(iface);

    TRACE("iface %p, filter %p.\n", iface, filter);

    stream->filter = filter;

    return S_OK;
}

static HRESULT WINAPI ddraw_IAMMediaStream_JoinFilterGraph(IAMMediaStream *iface, IFilterGraph *filtergraph)
{
    struct ddraw_stream *stream = impl_from_IAMMediaStream(iface);

    TRACE("stream %p, filtergraph %p.\n", stream, filtergraph);

    stream->graph = filtergraph;

    return S_OK;
}

static const struct IAMMediaStreamVtbl ddraw_IAMMediaStream_vtbl =
{
    /*** IUnknown methods ***/
    ddraw_IAMMediaStream_QueryInterface,
    ddraw_IAMMediaStream_AddRef,
    ddraw_IAMMediaStream_Release,
    /*** IMediaStream methods ***/
    ddraw_IAMMediaStream_GetMultiMediaStream,
    ddraw_IAMMediaStream_GetInformation,
    ddraw_IAMMediaStream_SetSameFormat,
    ddraw_IAMMediaStream_AllocateSample,
    ddraw_IAMMediaStream_CreateSharedSample,
    ddraw_IAMMediaStream_SendEndOfStream,
    /*** IAMMediaStream methods ***/
    ddraw_IAMMediaStream_Initialize,
    ddraw_IAMMediaStream_SetState,
    ddraw_IAMMediaStream_JoinAMMultiMediaStream,
    ddraw_IAMMediaStream_JoinFilter,
    ddraw_IAMMediaStream_JoinFilterGraph
};

static inline struct ddraw_stream *impl_from_IDirectDrawMediaStream(IDirectDrawMediaStream *iface)
{
    return CONTAINING_RECORD(iface, struct ddraw_stream, IDirectDrawMediaStream_iface);
}

/*** IUnknown methods ***/
static HRESULT WINAPI ddraw_IDirectDrawMediaStream_QueryInterface(IDirectDrawMediaStream *iface,
        REFIID riid, void **ret_iface)
{
    struct ddraw_stream *This = impl_from_IDirectDrawMediaStream(iface);
    TRACE("(%p/%p)->(%s,%p)\n", iface, This, debugstr_guid(riid), ret_iface);
    return IAMMediaStream_QueryInterface(&This->IAMMediaStream_iface, riid, ret_iface);
}

static ULONG WINAPI ddraw_IDirectDrawMediaStream_AddRef(IDirectDrawMediaStream *iface)
{
    struct ddraw_stream *This = impl_from_IDirectDrawMediaStream(iface);
    TRACE("(%p/%p)\n", iface, This);
    return IAMMediaStream_AddRef(&This->IAMMediaStream_iface);
}

static ULONG WINAPI ddraw_IDirectDrawMediaStream_Release(IDirectDrawMediaStream *iface)
{
    struct ddraw_stream *This = impl_from_IDirectDrawMediaStream(iface);
    TRACE("(%p/%p)\n", iface, This);
    return IAMMediaStream_Release(&This->IAMMediaStream_iface);
}

static HRESULT WINAPI ddraw_IDirectDrawMediaStream_GetMultiMediaStream(IDirectDrawMediaStream *iface,
        IMultiMediaStream **mmstream)
{
    struct ddraw_stream *stream = impl_from_IDirectDrawMediaStream(iface);
    return IAMMediaStream_GetMultiMediaStream(&stream->IAMMediaStream_iface, mmstream);
}

static HRESULT WINAPI ddraw_IDirectDrawMediaStream_GetInformation(IDirectDrawMediaStream *iface,
        MSPID *purpose_id, STREAM_TYPE *type)
{
    struct ddraw_stream *stream = impl_from_IDirectDrawMediaStream(iface);
    return IAMMediaStream_GetInformation(&stream->IAMMediaStream_iface, purpose_id, type);
}

static HRESULT WINAPI ddraw_IDirectDrawMediaStream_SetSameFormat(IDirectDrawMediaStream *iface,
        IMediaStream *other, DWORD flags)
{
    struct ddraw_stream *stream = impl_from_IDirectDrawMediaStream(iface);
    return IAMMediaStream_SetSameFormat(&stream->IAMMediaStream_iface, other, flags);
}

static HRESULT WINAPI ddraw_IDirectDrawMediaStream_AllocateSample(IDirectDrawMediaStream *iface,
        DWORD flags, IStreamSample **sample)
{
    struct ddraw_stream *stream = impl_from_IDirectDrawMediaStream(iface);
    return IAMMediaStream_AllocateSample(&stream->IAMMediaStream_iface, flags, sample);
}

static HRESULT WINAPI ddraw_IDirectDrawMediaStream_CreateSharedSample(IDirectDrawMediaStream *iface,
        IStreamSample *existing_sample, DWORD flags, IStreamSample **sample)
{
    struct ddraw_stream *stream = impl_from_IDirectDrawMediaStream(iface);
    return IAMMediaStream_CreateSharedSample(&stream->IAMMediaStream_iface, existing_sample, flags, sample);
}

static HRESULT WINAPI ddraw_IDirectDrawMediaStream_SendEndOfStream(IDirectDrawMediaStream *iface, DWORD flags)
{
    struct ddraw_stream *stream = impl_from_IDirectDrawMediaStream(iface);
    return IAMMediaStream_SendEndOfStream(&stream->IAMMediaStream_iface, flags);
}

/*** IDirectDrawMediaStream methods ***/
static HRESULT WINAPI ddraw_IDirectDrawMediaStream_GetFormat(IDirectDrawMediaStream *iface,
        DDSURFACEDESC *current_format, IDirectDrawPalette **palette,
        DDSURFACEDESC *desired_format, DWORD *flags)
{
    struct ddraw_stream *stream = impl_from_IDirectDrawMediaStream(iface);

    TRACE("stream %p, current_format %p, palette %p, desired_format %p, flags %p.\n", stream, current_format, palette,
            desired_format, flags);

    EnterCriticalSection(&stream->cs);

    if (!stream->peer)
    {
        LeaveCriticalSection(&stream->cs);
        return MS_E_NOSTREAM;
    }

    if (current_format)
    {
        current_format->dwFlags = stream->format.flags | DDSD_WIDTH | DDSD_HEIGHT | DDSD_CAPS;
        current_format->dwWidth = stream->format.width;
        current_format->dwHeight = stream->format.height;
        current_format->ddpfPixelFormat = stream->format.pf;
        current_format->ddsCaps.dwCaps = DDSCAPS_SYSTEMMEMORY | DDSCAPS_OFFSCREENPLAIN;
    }

    if (palette)
        *palette = NULL;

    if (desired_format)
    {
        desired_format->dwFlags = DDSD_WIDTH | DDSD_HEIGHT;
        desired_format->dwWidth = stream->format.width;
        desired_format->dwHeight = stream->format.height;
        desired_format->ddpfPixelFormat = stream->format.pf;
        desired_format->ddsCaps.dwCaps = DDSCAPS_SYSTEMMEMORY | DDSCAPS_OFFSCREENPLAIN;
    }

    if (flags)
        *flags = 0;

    LeaveCriticalSection(&stream->cs);

    return S_OK;
}

static unsigned int align(unsigned int n, unsigned int alignment)
{
    return (n + alignment - 1) & ~(alignment - 1);
}

static void set_mt_from_desc(AM_MEDIA_TYPE *mt, const DDSURFACEDESC *format, unsigned int pitch)
{
    VIDEOINFO *videoinfo = CoTaskMemAlloc(sizeof(VIDEOINFO));

    memset(mt, 0, sizeof(*mt));
    mt->majortype = MEDIATYPE_Video;
    mt->formattype = FORMAT_VideoInfo;
    mt->cbFormat = sizeof(VIDEOINFO);
    mt->pbFormat = (BYTE *)videoinfo;

    memset(videoinfo, 0, sizeof(*videoinfo));
    SetRect(&videoinfo->rcSource, 0, 0, format->dwWidth, format->dwHeight);
    SetRect(&videoinfo->rcTarget, 0, 0, format->dwWidth, format->dwHeight);
    videoinfo->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    videoinfo->bmiHeader.biWidth = pitch * 8 / format->ddpfPixelFormat.dwRGBBitCount;
    videoinfo->bmiHeader.biHeight = -format->dwHeight;
    videoinfo->bmiHeader.biBitCount = format->ddpfPixelFormat.dwRGBBitCount;
    videoinfo->bmiHeader.biCompression = BI_RGB;
    videoinfo->bmiHeader.biPlanes = 1;
    videoinfo->bmiHeader.biSizeImage = align(pitch * format->dwHeight, 4);

    mt->lSampleSize = videoinfo->bmiHeader.biSizeImage;
    mt->bFixedSizeSamples = TRUE;

    if (format->ddpfPixelFormat.dwRGBBitCount == 16 && format->ddpfPixelFormat.dwRBitMask == 0x7c00)
    {
        mt->subtype = MEDIASUBTYPE_RGB555;
    }
    else if (format->ddpfPixelFormat.dwRGBBitCount == 16 && format->ddpfPixelFormat.dwRBitMask == 0xf800)
    {
        mt->subtype = MEDIASUBTYPE_RGB565;
        videoinfo = (VIDEOINFO *)mt->pbFormat;
        videoinfo->bmiHeader.biCompression = BI_BITFIELDS;
        videoinfo->dwBitMasks[iRED]   = 0xf800;
        videoinfo->dwBitMasks[iGREEN] = 0x07e0;
        videoinfo->dwBitMasks[iBLUE]  = 0x001f;
    }
    else if (format->ddpfPixelFormat.dwRGBBitCount == 24)
    {
        mt->subtype = MEDIASUBTYPE_RGB24;
    }
    else if (format->ddpfPixelFormat.dwRGBBitCount == 32)
    {
        mt->subtype = MEDIASUBTYPE_RGB32;
    }
    else if (format->ddpfPixelFormat.dwRGBBitCount == 8 && (format->ddpfPixelFormat.dwFlags & DDPF_PALETTEINDEXED8))
    {
        mt->subtype = MEDIASUBTYPE_RGB8;
        videoinfo->bmiHeader.biClrUsed = 256;
        /* FIXME: Translate the palette. */
    }
    else
    {
        FIXME("Unknown flags %#lx, bit count %lu.\n",
                format->ddpfPixelFormat.dwFlags, format->ddpfPixelFormat.dwRGBBitCount);
    }
}

static HRESULT WINAPI ddraw_IDirectDrawMediaStream_SetFormat(IDirectDrawMediaStream *iface,
        const DDSURFACEDESC *format, IDirectDrawPalette *palette)
{
    struct ddraw_stream *stream = impl_from_IDirectDrawMediaStream(iface);
    struct format old_format;
    IPin *old_peer;
    HRESULT hr;

    TRACE("stream %p, format %p, palette %p.\n", stream, format, palette);

    if (palette)
        FIXME("Setting palette is not yet supported.\n");

    if (!format)
        return E_POINTER;

    if (format->dwSize != sizeof(DDSURFACEDESC))
        return E_INVALIDARG;

    TRACE("flags %#lx, pixel format flags %#lx, bit count %lu, size %lux%lu.\n",
            format->dwFlags, format->ddpfPixelFormat.dwFlags,
            format->ddpfPixelFormat.dwRGBBitCount, format->dwWidth, format->dwHeight);

    if (format->dwFlags & DDSD_PIXELFORMAT)
    {
        if (format->ddpfPixelFormat.dwSize != sizeof(DDPIXELFORMAT))
        {
            WARN("Invalid size %#lx, returning DDERR_INVALIDSURFACETYPE.\n", format->ddpfPixelFormat.dwSize);
            return DDERR_INVALIDSURFACETYPE;
        }

        if (format->ddpfPixelFormat.dwFlags & DDPF_FOURCC)
        {
            if (!format->ddpfPixelFormat.dwRGBBitCount)
            {
                WARN("Invalid zero bit count, returning E_INVALIDARG.\n");
                return E_INVALIDARG;
            }
        }
        else
        {
            if (format->ddpfPixelFormat.dwFlags & (DDPF_YUV | DDPF_PALETTEINDEXED1 |
                    DDPF_PALETTEINDEXED2 | DDPF_PALETTEINDEXED4 | DDPF_PALETTEINDEXEDTO8))
            {
                WARN("Rejecting flags %#lx.\n", format->ddpfPixelFormat.dwFlags);
                return DDERR_INVALIDSURFACETYPE;
            }

            if (!(format->ddpfPixelFormat.dwFlags & DDPF_RGB))
            {
                WARN("Rejecting non-RGB flags %#lx.\n", format->ddpfPixelFormat.dwFlags);
                return DDERR_INVALIDSURFACETYPE;
            }

            switch (format->ddpfPixelFormat.dwRGBBitCount)
            {
            case 8:
                if (!(format->ddpfPixelFormat.dwFlags & DDPF_PALETTEINDEXED8))
                {
                    WARN("Rejecting non-palettized 8-bit format.\n");
                    return DDERR_INVALIDSURFACETYPE;
                }
                break;
            case 16:
                if (format->ddpfPixelFormat.dwFlags & DDPF_PALETTEINDEXED8)
                {
                    WARN("Rejecting palettized 16-bit format.\n");
                    return DDERR_INVALIDSURFACETYPE;
                }
                if ((format->ddpfPixelFormat.dwRBitMask != 0x7c00 ||
                    format->ddpfPixelFormat.dwGBitMask != 0x03e0 ||
                    format->ddpfPixelFormat.dwBBitMask != 0x001f) &&
                    (format->ddpfPixelFormat.dwRBitMask != 0xf800 ||
                    format->ddpfPixelFormat.dwGBitMask != 0x07e0 ||
                    format->ddpfPixelFormat.dwBBitMask != 0x001f))
                {
                    WARN("Rejecting bit masks %08lx, %08lx, %08lx.\n",
                            format->ddpfPixelFormat.dwRBitMask,
                            format->ddpfPixelFormat.dwGBitMask,
                            format->ddpfPixelFormat.dwBBitMask);
                    return DDERR_INVALIDSURFACETYPE;
                }
                break;
            case 24:
            case 32:
                if (format->ddpfPixelFormat.dwFlags & DDPF_PALETTEINDEXED8)
                {
                    WARN("Rejecting palettized %lu-bit format.\n", format->ddpfPixelFormat.dwRGBBitCount);
                    return DDERR_INVALIDSURFACETYPE;
                }
                if (format->ddpfPixelFormat.dwRBitMask != 0xff0000 ||
                    format->ddpfPixelFormat.dwGBitMask != 0x00ff00 ||
                    format->ddpfPixelFormat.dwBBitMask != 0x0000ff)
                {
                    WARN("Rejecting bit masks %08lx, %08lx, %08lx.\n",
                            format->ddpfPixelFormat.dwRBitMask,
                            format->ddpfPixelFormat.dwGBitMask,
                            format->ddpfPixelFormat.dwBBitMask);
                    return DDERR_INVALIDSURFACETYPE;
                }
                break;
            default:
                WARN("Unknown bit count %lu.\n", format->ddpfPixelFormat.dwRGBBitCount);
                return DDERR_INVALIDSURFACETYPE;
            }
        }
    }

    EnterCriticalSection(&stream->cs);

    old_format = stream->format;
    stream->format.flags = format->dwFlags & (DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT);
    if (format->dwFlags & (DDSD_WIDTH | DDSD_HEIGHT))
    {
        stream->format.width = format->dwWidth;
        stream->format.height = format->dwHeight;
    }
    if (format->dwFlags & DDSD_PIXELFORMAT)
        stream->format.pf = format->ddpfPixelFormat;

    if (stream->peer && !is_format_compatible(stream, old_format.width, old_format.height, &old_format.pf))
    {
        AM_MEDIA_TYPE new_mt;

        if (stream->sample_refs > 0)
        {
            WARN("Outstanding sample references, returning MS_E_SAMPLEALLOC.\n");
            stream->format = old_format;
            LeaveCriticalSection(&stream->cs);
            return MS_E_SAMPLEALLOC;
        }

        set_mt_from_desc(&new_mt, format, format->dwWidth * format->ddpfPixelFormat.dwRGBBitCount / 8);

        if (!stream->using_private_allocator || IPin_QueryAccept(stream->peer, &new_mt) != S_OK)
        {
            AM_MEDIA_TYPE old_mt;

            /* Reconnect. */
            old_peer = stream->peer;
            IPin_AddRef(old_peer);
            CopyMediaType(&old_mt, &stream->mt);

            IFilterGraph_Disconnect(stream->graph, stream->peer);
            IFilterGraph_Disconnect(stream->graph, &stream->IPin_iface);
            if (FAILED(hr = IFilterGraph_ConnectDirect(stream->graph, old_peer, &stream->IPin_iface, NULL)))
            {
                stream->format = old_format;
                IFilterGraph_ConnectDirect(stream->graph, old_peer, &stream->IPin_iface, &old_mt);
                IPin_Release(old_peer);
                FreeMediaType(&old_mt);
                FreeMediaType(&new_mt);
                LeaveCriticalSection(&stream->cs);
                return DDERR_INVALIDSURFACETYPE;
            }
            FreeMediaType(&old_mt);
            IPin_Release(old_peer);
        }

        FreeMediaType(&new_mt);
    }

    LeaveCriticalSection(&stream->cs);

    return S_OK;
}

static HRESULT WINAPI ddraw_IDirectDrawMediaStream_GetDirectDraw(IDirectDrawMediaStream *iface,
        IDirectDraw **ddraw)
{
    struct ddraw_stream *stream = impl_from_IDirectDrawMediaStream(iface);

    TRACE("stream %p, ddraw %p.\n", stream, ddraw);

    if (!ddraw)
        return E_POINTER;

    if (!stream->ddraw)
    {
        *ddraw = NULL;
        return S_OK;
    }

    IDirectDraw_AddRef(stream->ddraw);
    *ddraw = stream->ddraw;

    return S_OK;
}

static HRESULT WINAPI ddraw_IDirectDrawMediaStream_SetDirectDraw(IDirectDrawMediaStream *iface,
        IDirectDraw *ddraw)
{
    struct ddraw_stream *stream = impl_from_IDirectDrawMediaStream(iface);

    TRACE("stream %p, ddraw %p.\n", stream, ddraw);

    EnterCriticalSection(&stream->cs);

    if (stream->sample_refs)
    {
        HRESULT hr = (stream->ddraw == ddraw) ? S_OK : MS_E_SAMPLEALLOC;
        LeaveCriticalSection(&stream->cs);
        return hr;
    }

    if (stream->ddraw)
        IDirectDraw_Release(stream->ddraw);

    if (ddraw)
    {
        IDirectDraw_AddRef(ddraw);
        stream->ddraw = ddraw;
    }
    else
        stream->ddraw = NULL;

    LeaveCriticalSection(&stream->cs);

    return S_OK;
}

static HRESULT WINAPI ddraw_IDirectDrawMediaStream_CreateSample(IDirectDrawMediaStream *iface,
        IDirectDrawSurface *surface, const RECT *rect, DWORD flags,
        IDirectDrawStreamSample **sample)
{
    struct ddraw_stream *stream = impl_from_IDirectDrawMediaStream(iface);
    HRESULT hr;

    TRACE("stream %p, surface %p, rect %s, flags %#lx, sample %p.\n",
            stream, surface, wine_dbgstr_rect(rect), flags, sample);

    if (!surface && rect)
        return E_INVALIDARG;

    EnterCriticalSection(&stream->cs);
    hr = ddrawstreamsample_create(stream, surface, rect, sample);
    LeaveCriticalSection(&stream->cs);

    return hr;
}

static HRESULT WINAPI ddraw_IDirectDrawMediaStream_GetTimePerFrame(IDirectDrawMediaStream *iface,
        STREAM_TIME *frame_time)
{
    struct ddraw_stream *stream = impl_from_IDirectDrawMediaStream(iface);

    TRACE("stream %p, frame_time %p.\n", stream, frame_time);

    if (!frame_time)
        return E_POINTER;

    EnterCriticalSection(&stream->cs);

    if (!stream->peer)
    {
        LeaveCriticalSection(&stream->cs);
        return MS_E_NOSTREAM;
    }

    *frame_time = ((VIDEOINFO *)stream->mt.pbFormat)->AvgTimePerFrame;

    LeaveCriticalSection(&stream->cs);

    return S_OK;
}

static const struct IDirectDrawMediaStreamVtbl ddraw_IDirectDrawMediaStream_Vtbl =
{
    /*** IUnknown methods ***/
    ddraw_IDirectDrawMediaStream_QueryInterface,
    ddraw_IDirectDrawMediaStream_AddRef,
    ddraw_IDirectDrawMediaStream_Release,
    /*** IMediaStream methods ***/
    ddraw_IDirectDrawMediaStream_GetMultiMediaStream,
    ddraw_IDirectDrawMediaStream_GetInformation,
    ddraw_IDirectDrawMediaStream_SetSameFormat,
    ddraw_IDirectDrawMediaStream_AllocateSample,
    ddraw_IDirectDrawMediaStream_CreateSharedSample,
    ddraw_IDirectDrawMediaStream_SendEndOfStream,
    /*** IDirectDrawMediaStream methods ***/
    ddraw_IDirectDrawMediaStream_GetFormat,
    ddraw_IDirectDrawMediaStream_SetFormat,
    ddraw_IDirectDrawMediaStream_GetDirectDraw,
    ddraw_IDirectDrawMediaStream_SetDirectDraw,
    ddraw_IDirectDrawMediaStream_CreateSample,
    ddraw_IDirectDrawMediaStream_GetTimePerFrame
};

struct enum_media_types
{
    IEnumMediaTypes IEnumMediaTypes_iface;
    LONG refcount;
    unsigned int index;
};

static const IEnumMediaTypesVtbl enum_media_types_vtbl;

static struct enum_media_types *impl_from_IEnumMediaTypes(IEnumMediaTypes *iface)
{
    return CONTAINING_RECORD(iface, struct enum_media_types, IEnumMediaTypes_iface);
}

static HRESULT WINAPI enum_media_types_QueryInterface(IEnumMediaTypes *iface, REFIID iid, void **out)
{
    TRACE("iface %p, iid %s, out %p.\n", iface, debugstr_guid(iid), out);

    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IEnumMediaTypes))
    {
        IEnumMediaTypes_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI enum_media_types_AddRef(IEnumMediaTypes *iface)
{
    struct enum_media_types *enum_media_types = impl_from_IEnumMediaTypes(iface);
    ULONG refcount = InterlockedIncrement(&enum_media_types->refcount);
    TRACE("%p increasing refcount to %lu.\n", enum_media_types, refcount);
    return refcount;
}

static ULONG WINAPI enum_media_types_Release(IEnumMediaTypes *iface)
{
    struct enum_media_types *enum_media_types = impl_from_IEnumMediaTypes(iface);
    ULONG refcount = InterlockedDecrement(&enum_media_types->refcount);
    TRACE("%p decreasing refcount to %lu.\n", enum_media_types, refcount);
    if (!refcount)
        free(enum_media_types);
    return refcount;
}

static HRESULT WINAPI enum_media_types_Next(IEnumMediaTypes *iface, ULONG count, AM_MEDIA_TYPE **mts, ULONG *ret_count)
{
    struct enum_media_types *enum_media_types = impl_from_IEnumMediaTypes(iface);

    TRACE("iface %p, count %lu, mts %p, ret_count %p.\n", iface, count, mts, ret_count);

    if (!ret_count)
        return E_POINTER;

    if (count && !enum_media_types->index)
    {
        mts[0] = CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE));
        memset(mts[0], 0, sizeof(AM_MEDIA_TYPE));
        mts[0]->majortype = MEDIATYPE_Video;
        mts[0]->subtype = MEDIASUBTYPE_RGB8;
        mts[0]->bFixedSizeSamples = TRUE;
        mts[0]->lSampleSize = 10000;
        ++enum_media_types->index;
        *ret_count = 1;
        return count == 1 ? S_OK : S_FALSE;
    }

    *ret_count = 0;
    return count ? S_FALSE : S_OK;
}

static HRESULT WINAPI enum_media_types_Skip(IEnumMediaTypes *iface, ULONG count)
{
    struct enum_media_types *enum_media_types = impl_from_IEnumMediaTypes(iface);

    TRACE("iface %p, count %lu.\n", iface, count);

    enum_media_types->index += count;

    return S_OK;
}

static HRESULT WINAPI enum_media_types_Reset(IEnumMediaTypes *iface)
{
    struct enum_media_types *enum_media_types = impl_from_IEnumMediaTypes(iface);

    TRACE("iface %p.\n", iface);

    enum_media_types->index = 0;
    return S_OK;
}

static HRESULT WINAPI enum_media_types_Clone(IEnumMediaTypes *iface, IEnumMediaTypes **out)
{
    struct enum_media_types *enum_media_types = impl_from_IEnumMediaTypes(iface);
    struct enum_media_types *object;

    TRACE("iface %p, out %p.\n", iface, out);

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    object->IEnumMediaTypes_iface.lpVtbl = &enum_media_types_vtbl;
    object->refcount = 1;
    object->index = enum_media_types->index;

    *out = &object->IEnumMediaTypes_iface;
    return S_OK;
}

static const IEnumMediaTypesVtbl enum_media_types_vtbl =
{
    enum_media_types_QueryInterface,
    enum_media_types_AddRef,
    enum_media_types_Release,
    enum_media_types_Next,
    enum_media_types_Skip,
    enum_media_types_Reset,
    enum_media_types_Clone,
};

static inline struct ddraw_stream *impl_from_IPin(IPin *iface)
{
    return CONTAINING_RECORD(iface, struct ddraw_stream, IPin_iface);
}

static HRESULT WINAPI ddraw_sink_QueryInterface(IPin *iface, REFIID iid, void **out)
{
    struct ddraw_stream *stream = impl_from_IPin(iface);
    return IAMMediaStream_QueryInterface(&stream->IAMMediaStream_iface, iid, out);
}

static ULONG WINAPI ddraw_sink_AddRef(IPin *iface)
{
    struct ddraw_stream *stream = impl_from_IPin(iface);
    return IAMMediaStream_AddRef(&stream->IAMMediaStream_iface);
}

static ULONG WINAPI ddraw_sink_Release(IPin *iface)
{
    struct ddraw_stream *stream = impl_from_IPin(iface);
    return IAMMediaStream_Release(&stream->IAMMediaStream_iface);
}

static HRESULT WINAPI ddraw_sink_Connect(IPin *iface, IPin *peer, const AM_MEDIA_TYPE *mt)
{
    WARN("iface %p, peer %p, mt %p, unexpected call!\n", iface, peer, mt);
    return E_UNEXPECTED;
}

static HRESULT WINAPI ddraw_sink_ReceiveConnection(IPin *iface, IPin *peer, const AM_MEDIA_TYPE *mt)
{
    struct ddraw_stream *stream = impl_from_IPin(iface);
    const VIDEOINFOHEADER *video_info;
    PIN_DIRECTION dir;
    DWORD width;
    DWORD height;
    DDPIXELFORMAT pf = {sizeof(DDPIXELFORMAT)};

    TRACE("stream %p, peer %p, mt %p.\n", stream, peer, mt);
    strmbase_dump_media_type(mt);

    EnterCriticalSection(&stream->cs);

    if (stream->peer)
    {
        LeaveCriticalSection(&stream->cs);
        return VFW_E_ALREADY_CONNECTED;
    }

    if (!IsEqualGUID(&mt->majortype, &MEDIATYPE_Video)
            || !IsEqualGUID(&mt->formattype, &FORMAT_VideoInfo))
    {
        LeaveCriticalSection(&stream->cs);
        return VFW_E_TYPE_NOT_ACCEPTED;
    }

    video_info = (const VIDEOINFOHEADER *)mt->pbFormat;

    width = video_info->bmiHeader.biWidth;
    height = abs(video_info->bmiHeader.biHeight);
    pf.dwFlags = DDPF_RGB;
    if (IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_RGB8))
    {
        pf.dwFlags |= DDPF_PALETTEINDEXED8;
        pf.dwRGBBitCount = 8;
    }
    else if (IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_RGB555))
    {
        pf.dwRGBBitCount = 16;
        pf.dwRBitMask = 0x7c00;
        pf.dwGBitMask = 0x03e0;
        pf.dwBBitMask = 0x001f;
    }
    else if (IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_RGB565))
    {
        pf.dwRGBBitCount = 16;
        pf.dwRBitMask = 0xf800;
        pf.dwGBitMask = 0x07e0;
        pf.dwBBitMask = 0x001f;
    }
    else if (IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_RGB24))
    {
        pf.dwRGBBitCount = 24;
        pf.dwRBitMask = 0xff0000;
        pf.dwGBitMask = 0x00ff00;
        pf.dwBBitMask = 0x0000ff;
    }
    else if (IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_RGB32))
    {
        pf.dwRGBBitCount = 32;
        pf.dwRBitMask = 0xff0000;
        pf.dwGBitMask = 0x00ff00;
        pf.dwBBitMask = 0x0000ff;
    }
    else
    {
        LeaveCriticalSection(&stream->cs);
        return VFW_E_TYPE_NOT_ACCEPTED;
    }

    if (!is_format_compatible(stream, width, height, &pf))
    {
        LeaveCriticalSection(&stream->cs);
        return VFW_E_TYPE_NOT_ACCEPTED;
    }

    IPin_QueryDirection(peer, &dir);
    if (dir != PINDIR_OUTPUT)
    {
        WARN("Rejecting connection from input pin.\n");
        LeaveCriticalSection(&stream->cs);
        return VFW_E_INVALID_DIRECTION;
    }

    CopyMediaType(&stream->mt, mt);
    IPin_AddRef(stream->peer = peer);

    stream->format.width = width;
    stream->format.height = height;
    if (!(stream->format.flags & DDSD_PIXELFORMAT))
        stream->format.pf = pf;

    LeaveCriticalSection(&stream->cs);

    return S_OK;
}

static HRESULT WINAPI ddraw_sink_Disconnect(IPin *iface)
{
    struct ddraw_stream *stream = impl_from_IPin(iface);

    TRACE("stream %p.\n", stream);

    EnterCriticalSection(&stream->cs);

    if (!stream->peer)
    {
        LeaveCriticalSection(&stream->cs);
        return S_FALSE;
    }

    IPin_Release(stream->peer);
    stream->peer = NULL;
    FreeMediaType(&stream->mt);
    memset(&stream->mt, 0, sizeof(AM_MEDIA_TYPE));

    LeaveCriticalSection(&stream->cs);

    return S_OK;
}

static HRESULT WINAPI ddraw_sink_ConnectedTo(IPin *iface, IPin **peer)
{
    struct ddraw_stream *stream = impl_from_IPin(iface);
    HRESULT hr;

    TRACE("stream %p, peer %p.\n", stream, peer);

    EnterCriticalSection(&stream->cs);

    if (stream->peer)
    {
        IPin_AddRef(*peer = stream->peer);
        hr = S_OK;
    }
    else
    {
        *peer = NULL;
        hr = VFW_E_NOT_CONNECTED;
    }

    LeaveCriticalSection(&stream->cs);

    return hr;
}

static HRESULT WINAPI ddraw_sink_ConnectionMediaType(IPin *iface, AM_MEDIA_TYPE *mt)
{
    struct ddraw_stream *stream = impl_from_IPin(iface);
    HRESULT hr;

    TRACE("stream %p, mt %p.\n", stream, mt);

    EnterCriticalSection(&stream->cs);

    if (stream->peer)
    {
        CopyMediaType(mt, &stream->mt);
        hr = S_OK;
    }
    else
    {
        memset(mt, 0, sizeof(AM_MEDIA_TYPE));
        hr = VFW_E_NOT_CONNECTED;
    }

    LeaveCriticalSection(&stream->cs);

    return hr;
}

static HRESULT WINAPI ddraw_sink_QueryPinInfo(IPin *iface, PIN_INFO *info)
{
    struct ddraw_stream *stream = impl_from_IPin(iface);

    TRACE("stream %p, info %p.\n", stream, info);

    IBaseFilter_AddRef(info->pFilter = (IBaseFilter *)stream->filter);
    info->dir = PINDIR_INPUT;
    wcscpy(info->achName, sink_id);

    return S_OK;
}

static HRESULT WINAPI ddraw_sink_QueryDirection(IPin *iface, PIN_DIRECTION *dir)
{
    TRACE("iface %p, dir %p.\n", iface, dir);
    *dir = PINDIR_INPUT;
    return S_OK;
}

static HRESULT WINAPI ddraw_sink_QueryId(IPin *iface, WCHAR **id)
{
    TRACE("iface %p, id %p.\n", iface, id);

    if (!(*id = CoTaskMemAlloc(sizeof(sink_id))))
        return E_OUTOFMEMORY;

    wcscpy(*id, sink_id);

    return S_OK;
}

static HRESULT WINAPI ddraw_sink_QueryAccept(IPin *iface, const AM_MEDIA_TYPE *mt)
{
    TRACE("iface %p, mt %p.\n", iface, mt);

    if (IsEqualGUID(&mt->majortype, &MEDIATYPE_Video)
            && IsEqualGUID(&mt->subtype, &MEDIASUBTYPE_RGB8)
            && IsEqualGUID(&mt->formattype, &FORMAT_VideoInfo))
        return S_OK;

    return VFW_E_TYPE_NOT_ACCEPTED;
}

static HRESULT WINAPI ddraw_sink_EnumMediaTypes(IPin *iface, IEnumMediaTypes **enum_media_types)
{
    struct enum_media_types *object;

    TRACE("iface %p, enum_media_types %p.\n", iface, enum_media_types);

    if (!enum_media_types)
        return E_POINTER;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    object->IEnumMediaTypes_iface.lpVtbl = &enum_media_types_vtbl;
    object->refcount = 1;
    object->index = 0;

    *enum_media_types = &object->IEnumMediaTypes_iface;
    return S_OK;
}

static HRESULT WINAPI ddraw_sink_QueryInternalConnections(IPin *iface, IPin **pins, ULONG *count)
{
    TRACE("iface %p, pins %p, count %p.\n", iface, pins, count);
    return E_NOTIMPL;
}

static HRESULT WINAPI ddraw_sink_EndOfStream(IPin *iface)
{
    struct ddraw_stream *stream = impl_from_IPin(iface);

    TRACE("stream %p.\n", stream);

    EnterCriticalSection(&stream->cs);

    if (stream->eos || stream->flushing)
    {
        LeaveCriticalSection(&stream->cs);
        return E_FAIL;
    }

    stream->eos = TRUE;

    flush_update_queue(stream, MS_S_ENDOFSTREAM);

    LeaveCriticalSection(&stream->cs);

    /* Calling IMediaStreamFilter::EndOfStream() inside the critical section
     * would invert the locking order, so we must leave it first to avoid
     * the streaming thread deadlocking on the filter's critical section. */
    IMediaStreamFilter_EndOfStream(stream->filter);

    return S_OK;
}

static HRESULT WINAPI ddraw_sink_BeginFlush(IPin *iface)
{
    struct ddraw_stream *stream = impl_from_IPin(iface);
    BOOL cancel_eos;

    TRACE("stream %p.\n", stream);

    EnterCriticalSection(&stream->cs);

    cancel_eos = stream->eos;

    stream->flushing = TRUE;
    stream->eos = FALSE;
    WakeConditionVariable(&stream->update_queued_cv);

    LeaveCriticalSection(&stream->cs);

    /* Calling IMediaStreamFilter::Flush() inside the critical section would
     * invert the locking order, so we must leave it first to avoid the
     * application thread deadlocking on the filter's critical section. */
    IMediaStreamFilter_Flush(stream->filter, cancel_eos);

    return S_OK;
}

static HRESULT WINAPI ddraw_sink_EndFlush(IPin *iface)
{
    struct ddraw_stream *stream = impl_from_IPin(iface);

    TRACE("stream %p.\n", stream);

    EnterCriticalSection(&stream->cs);

    stream->flushing = FALSE;

    LeaveCriticalSection(&stream->cs);

    return S_OK;
}

static HRESULT WINAPI ddraw_sink_NewSegment(IPin *iface, REFERENCE_TIME start, REFERENCE_TIME stop, double rate)
{
    struct ddraw_stream *stream = impl_from_IPin(iface);

    TRACE("stream %p, start %s, stop %s, rate %0.16e\n",
            stream, wine_dbgstr_longlong(start), wine_dbgstr_longlong(stop), rate);

    EnterCriticalSection(&stream->cs);

    stream->segment_start = start;

    LeaveCriticalSection(&stream->cs);

    return S_OK;
}

static const IPinVtbl ddraw_sink_vtbl =
{
    ddraw_sink_QueryInterface,
    ddraw_sink_AddRef,
    ddraw_sink_Release,
    ddraw_sink_Connect,
    ddraw_sink_ReceiveConnection,
    ddraw_sink_Disconnect,
    ddraw_sink_ConnectedTo,
    ddraw_sink_ConnectionMediaType,
    ddraw_sink_QueryPinInfo,
    ddraw_sink_QueryDirection,
    ddraw_sink_QueryId,
    ddraw_sink_QueryAccept,
    ddraw_sink_EnumMediaTypes,
    ddraw_sink_QueryInternalConnections,
    ddraw_sink_EndOfStream,
    ddraw_sink_BeginFlush,
    ddraw_sink_EndFlush,
    ddraw_sink_NewSegment,
};

static struct ddraw_stream *impl_from_IMemAllocator(IMemAllocator *iface)
{
    return CONTAINING_RECORD(iface, struct ddraw_stream, IMemAllocator_iface);
}

static HRESULT WINAPI ddraw_mem_allocator_QueryInterface(IMemAllocator *iface, REFIID iid, void **out)
{
    struct ddraw_stream *stream = impl_from_IMemAllocator(iface);
    return IAMMediaStream_QueryInterface(&stream->IAMMediaStream_iface, iid, out);
}

static ULONG WINAPI ddraw_mem_allocator_AddRef(IMemAllocator *iface)
{
    struct ddraw_stream *stream = impl_from_IMemAllocator(iface);
    return IAMMediaStream_AddRef(&stream->IAMMediaStream_iface);
}

static ULONG WINAPI ddraw_mem_allocator_Release(IMemAllocator *iface)
{
    struct ddraw_stream *stream = impl_from_IMemAllocator(iface);
    return IAMMediaStream_Release(&stream->IAMMediaStream_iface);
}

static HRESULT WINAPI ddraw_mem_allocator_SetProperties(IMemAllocator *iface,
        ALLOCATOR_PROPERTIES *req_props, ALLOCATOR_PROPERTIES *ret_props)
{
    struct ddraw_stream *stream = impl_from_IMemAllocator(iface);

    TRACE("stream %p, req_props %p, ret_props %p.\n", stream, req_props, ret_props);

    TRACE("Requested %ld buffers, size %ld, prefix %ld, alignment %ld.\n",
            req_props->cBuffers, req_props->cbBuffer, req_props->cbPrefix, req_props->cbAlign);

    if (!req_props->cbAlign)
        return VFW_E_BADALIGN;

    EnterCriticalSection(&stream->cs);

    if (stream->committed)
    {
        LeaveCriticalSection(&stream->cs);
        return VFW_E_ALREADY_COMMITTED;
    }

    stream->buffer_count = max(req_props->cBuffers, 1);

    ret_props->cBuffers = stream->buffer_count;
    ret_props->cbBuffer = stream->format.width * stream->format.height * stream->format.pf.dwRGBBitCount / 8;
    ret_props->cbAlign = 1;
    ret_props->cbPrefix = 0;

    LeaveCriticalSection(&stream->cs);

    return S_OK;
}

static HRESULT WINAPI ddraw_mem_allocator_GetProperties(IMemAllocator *iface, ALLOCATOR_PROPERTIES *props)
{
    struct ddraw_stream *stream = impl_from_IMemAllocator(iface);

    TRACE("stream %p, props %p.\n", stream, props);

    EnterCriticalSection(&stream->cs);
    props->cBuffers = stream->buffer_count;
    props->cbBuffer = stream->format.width * stream->format.height * stream->format.pf.dwRGBBitCount / 8;
    props->cbAlign = 1;
    props->cbPrefix = 0;
    LeaveCriticalSection(&stream->cs);

    return S_OK;
}

static HRESULT WINAPI ddraw_mem_allocator_Commit(IMemAllocator *iface)
{
    struct ddraw_stream *stream = impl_from_IMemAllocator(iface);

    TRACE("stream %p.\n", stream);

    EnterCriticalSection(&stream->cs);
    stream->committed = true;
    /* We have nothing to actually commit; all of our samples are created by
     * CreateSample(). */
    LeaveCriticalSection(&stream->cs);

    return S_OK;
}

static HRESULT WINAPI ddraw_mem_allocator_Decommit(IMemAllocator *iface)
{
    struct ddraw_stream *stream = impl_from_IMemAllocator(iface);

    TRACE("stream %p.\n", stream);

    EnterCriticalSection(&stream->cs);
    stream->committed = false;
    WakeAllConditionVariable(&stream->allocator_cv);
    /* We have nothing to actually decommit; all of our samples are created by
     * CreateSample(). */
    LeaveCriticalSection(&stream->cs);

    return S_OK;
}

static struct ddraw_sample *get_pending_sample(struct ddraw_stream *stream)
{
    struct ddraw_sample *sample;

    LIST_FOR_EACH_ENTRY(sample, &stream->update_queue, struct ddraw_sample, entry)
    {
        if (!sample->media_sample_refcount)
            return sample;
    }

    return NULL;
}

static HRESULT WINAPI ddraw_mem_allocator_GetBuffer(IMemAllocator *iface,
        IMediaSample **ret_sample, REFERENCE_TIME *start, REFERENCE_TIME *end, DWORD flags)
{
    struct ddraw_stream *stream = impl_from_IMemAllocator(iface);
    struct ddraw_sample *sample;
    HRESULT hr;

    TRACE("stream %p, ret_sample %p, start %p, end %p, flags %#lx.\n", stream, ret_sample, start, end, flags);

    EnterCriticalSection(&stream->cs);

    while (stream->committed && !(sample = get_pending_sample(stream)))
        SleepConditionVariableCS(&stream->allocator_cv, &stream->cs, INFINITE);

    if (!stream->committed)
    {
        LeaveCriticalSection(&stream->cs);
        return VFW_E_NOT_COMMITTED;
    }

    sample->surface_desc.dwSize = sizeof(DDSURFACEDESC);
    if ((FAILED(hr = IDirectDrawSurface_Lock(sample->surface,
            &sample->rect, &sample->surface_desc, DDLOCK_WAIT, NULL))))
    {
        LeaveCriticalSection(&stream->cs);
        return hr;
    }

    /* Only these fields are reset. */
    sample->sync_point = true;
    sample->discontinuity = false;

    sample->media_sample_refcount = 1;

    LeaveCriticalSection(&stream->cs);

    *ret_sample = &sample->IMediaSample_iface;
    return S_OK;
}

static HRESULT WINAPI ddraw_mem_allocator_ReleaseBuffer(IMemAllocator *iface, IMediaSample *sample)
{
    struct ddraw_stream *stream = impl_from_IMemAllocator(iface);

    FIXME("stream %p, sample %p, stub!\n", stream, sample);

    return E_NOTIMPL;
}

static const IMemAllocatorVtbl ddraw_mem_allocator_vtbl =
{
    ddraw_mem_allocator_QueryInterface,
    ddraw_mem_allocator_AddRef,
    ddraw_mem_allocator_Release,
    ddraw_mem_allocator_SetProperties,
    ddraw_mem_allocator_GetProperties,
    ddraw_mem_allocator_Commit,
    ddraw_mem_allocator_Decommit,
    ddraw_mem_allocator_GetBuffer,
    ddraw_mem_allocator_ReleaseBuffer,
};

static inline struct ddraw_stream *impl_from_IMemInputPin(IMemInputPin *iface)
{
    return CONTAINING_RECORD(iface, struct ddraw_stream, IMemInputPin_iface);
}

static HRESULT WINAPI ddraw_meminput_QueryInterface(IMemInputPin *iface, REFIID iid, void **out)
{
    struct ddraw_stream *stream = impl_from_IMemInputPin(iface);
    return IAMMediaStream_QueryInterface(&stream->IAMMediaStream_iface, iid, out);
}

static ULONG WINAPI ddraw_meminput_AddRef(IMemInputPin *iface)
{
    struct ddraw_stream *stream = impl_from_IMemInputPin(iface);
    return IAMMediaStream_AddRef(&stream->IAMMediaStream_iface);
}

static ULONG WINAPI ddraw_meminput_Release(IMemInputPin *iface)
{
    struct ddraw_stream *stream = impl_from_IMemInputPin(iface);
    return IAMMediaStream_Release(&stream->IAMMediaStream_iface);
}

static HRESULT WINAPI ddraw_meminput_GetAllocator(IMemInputPin *iface, IMemAllocator **allocator)
{
    struct ddraw_stream *stream = impl_from_IMemInputPin(iface);

    TRACE("stream %p, allocator %p.\n", stream, allocator);

    IMemAllocator_AddRef(*allocator = &stream->IMemAllocator_iface);
    return S_OK;
}

static HRESULT WINAPI ddraw_meminput_NotifyAllocator(IMemInputPin *iface, IMemAllocator *allocator, BOOL readonly)
{
    struct ddraw_stream *stream = impl_from_IMemInputPin(iface);

    TRACE("stream %p, allocator %p, readonly %d.\n", stream, allocator, readonly);

    if (!allocator)
        return E_POINTER;

    stream->using_private_allocator = (allocator == &stream->IMemAllocator_iface);

    return S_OK;
}

static HRESULT WINAPI ddraw_meminput_GetAllocatorRequirements(IMemInputPin *iface, ALLOCATOR_PROPERTIES *props)
{
    TRACE("iface %p, props %p.\n", iface, props);
    return E_NOTIMPL;
}

static struct ddraw_sample *get_update_sample(struct ddraw_stream *stream, IMediaSample *buffer)
{
    const BITMAPINFOHEADER *bitmap_info = &((VIDEOINFOHEADER *)stream->mt.pbFormat)->bmiHeader;
    STREAM_TIME start_stream_time, end_stream_time;
    REFERENCE_TIME start_time = 0, end_time = 0;
    struct ddraw_sample *sample;
    BYTE *top_down_pointer;
    int top_down_stride;
    BYTE *pointer;
    int stride;

    /* Is it any of the samples we gave out from GetBuffer()? */
    LIST_FOR_EACH_ENTRY(sample, &stream->update_queue, struct ddraw_sample, entry)
    {
        if (buffer == &sample->IMediaSample_iface)
        {
            sample->update_hr = S_OK;
            return sample;
        }
    }

    /* Find an unused sample and blit to it. */

    IMediaSample_GetPointer(buffer, &pointer);
    IMediaSample_GetTime(buffer, &start_time, &end_time);

    start_stream_time = start_time + stream->segment_start;
    end_stream_time = end_time + stream->segment_start;

    stride = ((bitmap_info->biWidth * bitmap_info->biBitCount + 31) & ~31) / 8;
    if (bitmap_info->biHeight < 0)
    {
        top_down_stride = stride;
        top_down_pointer = pointer;
    }
    else
    {
        top_down_stride = -stride;
        top_down_pointer = pointer + stride * (bitmap_info->biHeight - 1);
    }

    if ((sample = get_pending_sample(stream)))
    {
        sample->update_hr = copy_sample(sample, top_down_stride, top_down_pointer,
                start_stream_time, end_stream_time);
        return sample;
    }

    /* We don't have a sample yet. */
    return NULL;
}

static HRESULT WINAPI ddraw_meminput_Receive(IMemInputPin *iface, IMediaSample *buffer)
{
    struct ddraw_stream *stream = impl_from_IMemInputPin(iface);
    REFERENCE_TIME start_time = 0, end_time = 0;
    IMediaStreamFilter *filter;
    STREAM_TIME current_time;

    TRACE("stream %p, buffer %p.\n", stream, buffer);

    IMediaSample_GetTime(buffer, &start_time, &end_time);

    EnterCriticalSection(&stream->cs);

    if (stream->state == State_Stopped)
    {
        LeaveCriticalSection(&stream->cs);
        return S_OK;
    }
    if (stream->flushing)
    {
        LeaveCriticalSection(&stream->cs);
        return S_FALSE;
    }

    filter = stream->filter;

    LeaveCriticalSection(&stream->cs);
    if (S_OK == IMediaStreamFilter_GetCurrentStreamTime(filter, &current_time)
            && start_time >= current_time + 10000)
        IMediaStreamFilter_WaitUntil(filter, start_time);
    EnterCriticalSection(&stream->cs);

    for (;;)
    {
        struct ddraw_sample *sample;
        IQualityControl *qc;

        if (stream->state == State_Stopped)
        {
            LeaveCriticalSection(&stream->cs);
            return S_OK;
        }
        if (stream->flushing)
        {
            LeaveCriticalSection(&stream->cs);
            return S_FALSE;
        }

        if ((sample = get_update_sample(stream, buffer)))
        {
            if (sample->continuous_update && SUCCEEDED(sample->update_hr))
            {
                list_remove(&sample->entry);
                list_add_tail(&sample->parent->update_queue, &sample->entry);
            }
            else
            {
                remove_queued_update(sample);
            }

            if (IMediaStreamFilter_GetCurrentStreamTime(filter, &current_time) == S_OK
                    && SUCCEEDED(IPin_QueryInterface(stream->peer, &IID_IQualityControl, (void **)&qc)))
            {
                Quality q;
                q.Type = Famine;
                q.Proportion = 1000;
                q.Late = current_time - start_time;
                q.TimeStamp = start_time;
                IQualityControl_Notify(qc, (IBaseFilter *)stream->filter, q);
                IQualityControl_Release(qc);
            }

            LeaveCriticalSection(&stream->cs);
            return S_OK;
        }

        SleepConditionVariableCS(&stream->update_queued_cv, &stream->cs, INFINITE);
    }
}

static HRESULT WINAPI ddraw_meminput_ReceiveMultiple(IMemInputPin *iface,
        IMediaSample **samples, LONG count, LONG *processed)
{
    FIXME("iface %p, samples %p, count %lu, processed %p, stub!\n", iface, samples, count, processed);
    return E_NOTIMPL;
}

static HRESULT WINAPI ddraw_meminput_ReceiveCanBlock(IMemInputPin *iface)
{
    TRACE("iface %p.\n", iface);
    return S_OK;
}

static const IMemInputPinVtbl ddraw_meminput_vtbl =
{
    ddraw_meminput_QueryInterface,
    ddraw_meminput_AddRef,
    ddraw_meminput_Release,
    ddraw_meminput_GetAllocator,
    ddraw_meminput_NotifyAllocator,
    ddraw_meminput_GetAllocatorRequirements,
    ddraw_meminput_Receive,
    ddraw_meminput_ReceiveMultiple,
    ddraw_meminput_ReceiveCanBlock,
};

HRESULT ddraw_stream_create(IUnknown *outer, void **out)
{
    struct ddraw_stream *object;

    if (outer)
        return CLASS_E_NOAGGREGATION;

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    object->IAMMediaStream_iface.lpVtbl = &ddraw_IAMMediaStream_vtbl;
    object->IDirectDrawMediaStream_iface.lpVtbl = &ddraw_IDirectDrawMediaStream_Vtbl;
    object->IMemInputPin_iface.lpVtbl = &ddraw_meminput_vtbl;
    object->IMemAllocator_iface.lpVtbl = &ddraw_mem_allocator_vtbl;
    object->IPin_iface.lpVtbl = &ddraw_sink_vtbl;
    object->IMemAllocator_iface.lpVtbl = &ddraw_mem_allocator_vtbl;
    object->ref = 1;

    object->format.width = 100;
    object->format.height = 100;

    object->using_private_allocator = TRUE;
    InitializeConditionVariable(&object->allocator_cv);
    object->buffer_count = 1;

    InitializeCriticalSection(&object->cs);
    InitializeConditionVariable(&object->update_queued_cv);
    list_init(&object->update_queue);

    TRACE("Created ddraw stream %p.\n", object);

    *out = &object->IAMMediaStream_iface;

    return S_OK;
}

static inline struct ddraw_sample *impl_from_IDirectDrawStreamSample(IDirectDrawStreamSample *iface)
{
    return CONTAINING_RECORD(iface, struct ddraw_sample, IDirectDrawStreamSample_iface);
}

/*** IUnknown methods ***/
static HRESULT WINAPI ddraw_sample_QueryInterface(IDirectDrawStreamSample *iface,
        REFIID riid, void **ret_iface)
{
    TRACE("(%p)->(%s,%p)\n", iface, debugstr_guid(riid), ret_iface);

    if (IsEqualGUID(riid, &IID_IUnknown) ||
        IsEqualGUID(riid, &IID_IStreamSample) ||
        IsEqualGUID(riid, &IID_IDirectDrawStreamSample))
    {
        IDirectDrawStreamSample_AddRef(iface);
        *ret_iface = iface;
        return S_OK;
    }

    *ret_iface = NULL;

    ERR("(%p)->(%s,%p),not found\n", iface, debugstr_guid(riid), ret_iface);
    return E_NOINTERFACE;
}

static ULONG WINAPI ddraw_sample_AddRef(IDirectDrawStreamSample *iface)
{
    struct ddraw_sample *sample = impl_from_IDirectDrawStreamSample(iface);
    ULONG ref = InterlockedIncrement(&sample->ref);

    TRACE("(%p)->(): new ref = %lu\n", iface, ref);

    return ref;
}

static ULONG WINAPI ddraw_sample_Release(IDirectDrawStreamSample *iface)
{
    struct ddraw_sample *sample = impl_from_IDirectDrawStreamSample(iface);
    ULONG ref = InterlockedDecrement(&sample->ref);

    TRACE("(%p)->(): new ref = %lu\n", iface, ref);

    if (!ref)
    {
        EnterCriticalSection(&sample->parent->cs);

        if (sample->pending)
            remove_queued_update(sample);

        while (sample->media_sample_refcount)
            SleepConditionVariableCS(&sample->parent->allocator_cv, &sample->parent->cs, INFINITE);

        --sample->parent->sample_refs;
        LeaveCriticalSection(&sample->parent->cs);

        if (sample->mmstream)
            IMultiMediaStream_Release(sample->mmstream);
        IAMMediaStream_Release(&sample->parent->IAMMediaStream_iface);

        if (sample->surface)
            IDirectDrawSurface_Release(sample->surface);
        free(sample);
    }

    return ref;
}

/*** IStreamSample methods ***/
static HRESULT WINAPI ddraw_sample_GetMediaStream(IDirectDrawStreamSample *iface, IMediaStream **media_stream)
{
    struct ddraw_sample *sample = impl_from_IDirectDrawStreamSample(iface);

    TRACE("sample %p, media_stream %p.\n", sample, media_stream);

    if (!media_stream)
        return E_POINTER;

    IAMMediaStream_AddRef(&sample->parent->IAMMediaStream_iface);
    *media_stream = (IMediaStream *)&sample->parent->IAMMediaStream_iface;

    return S_OK;
}

static HRESULT WINAPI ddraw_sample_GetSampleTimes(IDirectDrawStreamSample *iface, STREAM_TIME *start_time,
                                                                 STREAM_TIME *end_time, STREAM_TIME *current_time)
{
    struct ddraw_sample *sample = impl_from_IDirectDrawStreamSample(iface);

    TRACE("sample %p, start_time %p, end_time %p, current_time %p.\n", sample, start_time, end_time, current_time);

    if (current_time)
        IMediaStreamFilter_GetCurrentStreamTime(sample->parent->filter, current_time);

    if (start_time)
        *start_time = sample->start_time;
    if (end_time)
        *end_time = sample->end_time;

    return S_OK;
}

static HRESULT WINAPI ddraw_sample_SetSampleTimes(IDirectDrawStreamSample *iface, const STREAM_TIME *start_time,
                                                                 const STREAM_TIME *end_time)
{
    FIXME("(%p)->(%p,%p): stub\n", iface, start_time, end_time);

    return E_NOTIMPL;
}

static HRESULT WINAPI ddraw_sample_Update(IDirectDrawStreamSample *iface,
        DWORD flags, HANDLE event, PAPCFUNC apc_func, DWORD apc_data)
{
    struct ddraw_sample *sample = impl_from_IDirectDrawStreamSample(iface);

    TRACE("sample %p, flags %#lx, event %p, apc_func %p, apc_data %#lx.\n",
            sample, flags, event, apc_func, apc_data);

    if (event && apc_func)
        return E_INVALIDARG;

    if (apc_func)
    {
        FIXME("APC support is not implemented!\n");
        return E_NOTIMPL;
    }

    EnterCriticalSection(&sample->parent->cs);

    if (sample->parent->state != State_Running)
    {
        LeaveCriticalSection(&sample->parent->cs);
        return MS_E_NOTRUNNING;
    }
    if (!sample->parent->peer || sample->parent->eos)
    {
        sample->update_hr = MS_S_ENDOFSTREAM;
        LeaveCriticalSection(&sample->parent->cs);
        return MS_S_ENDOFSTREAM;
    }
    if (sample->pending || sample->media_sample_refcount)
    {
        LeaveCriticalSection(&sample->parent->cs);
        return MS_E_BUSY;
    }

    sample->continuous_update = (flags & SSUPDATE_ASYNC) && (flags & SSUPDATE_CONTINUOUS);

    sample->update_hr = MS_S_NOUPDATE;
    sample->pending = true;
    sample->external_event = event;
    list_add_tail(&sample->parent->update_queue, &sample->entry);
    WakeConditionVariable(&sample->parent->update_queued_cv);
    WakeConditionVariable(&sample->parent->allocator_cv);

    if ((flags & SSUPDATE_ASYNC) || event)
    {
        LeaveCriticalSection(&sample->parent->cs);
        return MS_S_PENDING;
    }

    while (sample->pending || sample->media_sample_refcount)
        SleepConditionVariableCS(&sample->update_cv, &sample->parent->cs, INFINITE);

    LeaveCriticalSection(&sample->parent->cs);

    return sample->update_hr;
}

static HRESULT WINAPI ddraw_sample_CompletionStatus(IDirectDrawStreamSample *iface, DWORD flags, DWORD milliseconds)
{
    struct ddraw_sample *sample = impl_from_IDirectDrawStreamSample(iface);
    HRESULT hr;

    TRACE("sample %p, flags %#lx, milliseconds %lu.\n", sample, flags, milliseconds);

    EnterCriticalSection(&sample->parent->cs);

    if (sample->pending || sample->media_sample_refcount)
    {
        if (flags & (COMPSTAT_NOUPDATEOK | COMPSTAT_ABORT))
        {
            if (!sample->media_sample_refcount)
                remove_queued_update(sample);
        }
        else if (flags & COMPSTAT_WAIT)
        {
            DWORD start_time = GetTickCount();
            DWORD elapsed = 0;
            sample->continuous_update = FALSE;
            while ((sample->pending || sample->media_sample_refcount) && elapsed < milliseconds)
            {
                DWORD sleep_time = milliseconds - elapsed;
                if (!SleepConditionVariableCS(&sample->update_cv, &sample->parent->cs, sleep_time))
                    break;
                elapsed = GetTickCount() - start_time;
            }
        }
    }

    if (sample->pending || sample->media_sample_refcount)
        hr = MS_S_PENDING;
    else
        hr = sample->update_hr;

    LeaveCriticalSection(&sample->parent->cs);

    return hr;
}

/*** IDirectDrawStreamSample methods ***/
static HRESULT WINAPI ddraw_sample_GetSurface(IDirectDrawStreamSample *iface, IDirectDrawSurface **ddraw_surface,
                                                             RECT *rect)
{
    struct ddraw_sample *sample = impl_from_IDirectDrawStreamSample(iface);

    TRACE("(%p)->(%p,%p)\n", iface, ddraw_surface, rect);

    if (ddraw_surface)
    {
        *ddraw_surface = sample->surface;
        if (*ddraw_surface)
            IDirectDrawSurface_AddRef(*ddraw_surface);
    }

    if (rect)
        *rect = sample->rect;

    return S_OK;
}

static HRESULT WINAPI ddraw_sample_SetRect(IDirectDrawStreamSample *iface, const RECT *rect)
{
    FIXME("(%p)->(%p): stub\n", iface, rect);

    return E_NOTIMPL;
}

static const struct IDirectDrawStreamSampleVtbl DirectDrawStreamSample_Vtbl =
{
    /*** IUnknown methods ***/
    ddraw_sample_QueryInterface,
    ddraw_sample_AddRef,
    ddraw_sample_Release,
    /*** IStreamSample methods ***/
    ddraw_sample_GetMediaStream,
    ddraw_sample_GetSampleTimes,
    ddraw_sample_SetSampleTimes,
    ddraw_sample_Update,
    ddraw_sample_CompletionStatus,
    /*** IDirectDrawStreamSample methods ***/
    ddraw_sample_GetSurface,
    ddraw_sample_SetRect
};

static struct ddraw_sample *impl_from_IMediaSample(IMediaSample *iface)
{
    return CONTAINING_RECORD(iface, struct ddraw_sample, IMediaSample_iface);
}

static HRESULT WINAPI media_sample_QueryInterface(IMediaSample *iface, REFIID iid, void **out)
{
    struct ddraw_sample *sample = impl_from_IMediaSample(iface);

    TRACE("sample %p, iid %s, out %p.\n", sample, debugstr_guid(iid), out);

    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &IID_IMediaSample))
    {
        IMediaSample_AddRef(iface);
        *out = iface;
        return S_OK;
    }

    WARN("%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid(iid));
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI media_sample_AddRef(IMediaSample *iface)
{
    struct ddraw_sample *sample = impl_from_IMediaSample(iface);
    ULONG refcount;

    EnterCriticalSection(&sample->parent->cs);
    refcount = ++sample->media_sample_refcount;
    LeaveCriticalSection(&sample->parent->cs);

    TRACE("%p increasing refcount to %lu.\n", sample, refcount);

    return refcount;
}

static ULONG WINAPI media_sample_Release(IMediaSample *iface)
{
    struct ddraw_sample *sample = impl_from_IMediaSample(iface);
    ULONG refcount;

    EnterCriticalSection(&sample->parent->cs);

    /* This is not InterlockedDecrement(), because it's used in GetBuffer()
     * among other functions and therefore needs to be protected by the CS
     * anyway. */
    refcount = --sample->media_sample_refcount;

    TRACE("%p decreasing refcount to %lu.\n", sample, refcount);

    if (!refcount)
    {
        IDirectDrawSurface_Unlock(sample->surface, NULL);

        WakeConditionVariable(&sample->update_cv);

        /* This sample is not released back to the pool if it's no longer
         * pending an update.
         *
         * Use WakeAll, even though we're only releasing one sample, because we
         * also potentially need to wake up a thread stuck in
         * IDirectDrawStreamSample::Release().
         * This is arguably wasteful, but in practice we're unlikely to have
         * more than one thread in GetBuffer() at a time anyway. */
        if (sample->pending)
            WakeAllConditionVariable(&sample->parent->allocator_cv);
    }

    LeaveCriticalSection(&sample->parent->cs);
    return refcount;
}

static unsigned int get_sample_size(struct ddraw_sample *sample)
{
    return sample->surface_desc.lPitch * sample->surface_desc.dwHeight;
}

static HRESULT WINAPI media_sample_GetPointer(IMediaSample *iface, BYTE **data)
{
    struct ddraw_sample *sample = impl_from_IMediaSample(iface);

    TRACE("sample %p, data %p.\n", sample, data);

    *data = sample->surface_desc.lpSurface;
    return S_OK;
}

static LONG WINAPI media_sample_GetSize(IMediaSample *iface)
{
    struct ddraw_sample *sample = impl_from_IMediaSample(iface);

    TRACE("sample %p.\n", sample);

    return get_sample_size(sample);
}

static HRESULT WINAPI media_sample_GetTime(IMediaSample *iface, REFERENCE_TIME *start, REFERENCE_TIME *end)
{
    struct ddraw_sample *sample = impl_from_IMediaSample(iface);

    TRACE("sample %p, start %p, end %p.\n", sample, start, end);

    EnterCriticalSection(&sample->parent->cs);
    *start = sample->start_time;
    *end = sample->end_time;
    LeaveCriticalSection(&sample->parent->cs);

    return S_OK;
}

static HRESULT WINAPI media_sample_SetTime(IMediaSample *iface, REFERENCE_TIME *start, REFERENCE_TIME *end)
{
    struct ddraw_sample *sample = impl_from_IMediaSample(iface);

    TRACE("sample %p, start %p, end %p.\n", sample, start, end);

    EnterCriticalSection(&sample->parent->cs);
    if (start)
        sample->start_time = *start;
    if (end)
        sample->end_time = *end;
    LeaveCriticalSection(&sample->parent->cs);

    return S_OK;
}

static HRESULT WINAPI media_sample_IsSyncPoint(IMediaSample *iface)
{
    struct ddraw_sample *sample = impl_from_IMediaSample(iface);
    HRESULT hr;

    TRACE("sample %p.\n", sample);

    EnterCriticalSection(&sample->parent->cs);
    hr = sample->sync_point ? S_OK : S_FALSE;
    LeaveCriticalSection(&sample->parent->cs);

    return hr;
}

static HRESULT WINAPI media_sample_SetSyncPoint(IMediaSample *iface, BOOL sync_point)
{
    struct ddraw_sample *sample = impl_from_IMediaSample(iface);

    TRACE("sample %p, sync_point %d.\n", sample, sync_point);

    EnterCriticalSection(&sample->parent->cs);
    sample->sync_point = sync_point;
    LeaveCriticalSection(&sample->parent->cs);

    return S_OK;
}

static HRESULT WINAPI media_sample_IsPreroll(IMediaSample *iface)
{
    struct ddraw_sample *sample = impl_from_IMediaSample(iface);
    HRESULT hr;

    TRACE("sample %p.\n", sample);

    EnterCriticalSection(&sample->parent->cs);
    hr = sample->preroll ? S_OK : S_FALSE;
    LeaveCriticalSection(&sample->parent->cs);

    return hr;
}

static HRESULT WINAPI media_sample_SetPreroll(IMediaSample *iface, BOOL preroll)
{
    struct ddraw_sample *sample = impl_from_IMediaSample(iface);

    TRACE("sample %p, preroll %d.\n", sample, preroll);

    EnterCriticalSection(&sample->parent->cs);
    sample->preroll = preroll;
    LeaveCriticalSection(&sample->parent->cs);

    return S_OK;
}

static LONG WINAPI media_sample_GetActualDataLength(IMediaSample *iface)
{
    struct ddraw_sample *sample = impl_from_IMediaSample(iface);

    TRACE("sample %p.\n", sample);

    return get_sample_size(sample);
}

static HRESULT WINAPI media_sample_SetActualDataLength(IMediaSample *iface, LONG length)
{
    struct ddraw_sample *sample = impl_from_IMediaSample(iface);

    TRACE("sample %p, length %ld.\n", sample, length);

    return (length == get_sample_size(sample) ? S_OK : E_FAIL);
}

static HRESULT WINAPI media_sample_GetMediaType(IMediaSample *iface, AM_MEDIA_TYPE **ret_mt)
{
    struct ddraw_sample *sample = impl_from_IMediaSample(iface);

    TRACE("sample %p, ret_mt %p.\n", sample, ret_mt);

    if (!(*ret_mt = CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE))))
        return E_OUTOFMEMORY;
    set_mt_from_desc(*ret_mt, &sample->surface_desc, sample->surface_desc.lPitch);
    return S_OK;
}

static HRESULT WINAPI media_sample_SetMediaType(IMediaSample *iface, AM_MEDIA_TYPE *mt)
{
    struct ddraw_sample *sample = impl_from_IMediaSample(iface);

    FIXME("sample %p, mt %p, stub!\n", sample, mt);
    strmbase_dump_media_type(mt);

    return E_NOTIMPL;
}

static HRESULT WINAPI media_sample_IsDiscontinuity(IMediaSample *iface)
{
    struct ddraw_sample *sample = impl_from_IMediaSample(iface);
    HRESULT hr;

    TRACE("sample %p.\n", sample);

    EnterCriticalSection(&sample->parent->cs);
    hr = sample->discontinuity ? S_OK : S_FALSE;
    LeaveCriticalSection(&sample->parent->cs);

    return hr;
}

static HRESULT WINAPI media_sample_SetDiscontinuity(IMediaSample *iface, BOOL discontinuity)
{
    struct ddraw_sample *sample = impl_from_IMediaSample(iface);

    TRACE("sample %p, discontinuity %d.\n", sample, discontinuity);

    EnterCriticalSection(&sample->parent->cs);
    sample->discontinuity = discontinuity;
    LeaveCriticalSection(&sample->parent->cs);

    return S_OK;
}

static HRESULT WINAPI media_sample_GetMediaTime(IMediaSample *iface, LONGLONG *start, LONGLONG *end)
{
    struct ddraw_sample *sample = impl_from_IMediaSample(iface);

    TRACE("sample %p, start %p, end %p, not implemented.\n", sample, start, end);

    return E_NOTIMPL;
}

static HRESULT WINAPI media_sample_SetMediaTime(IMediaSample *iface, LONGLONG *start, LONGLONG *end)
{
    struct ddraw_sample *sample = impl_from_IMediaSample(iface);

    TRACE("sample %p, start %p, end %p, not implemented.\n", sample, start, end);

    return E_NOTIMPL;
}

static const struct IMediaSampleVtbl media_sample_vtbl =
{
    media_sample_QueryInterface,
    media_sample_AddRef,
    media_sample_Release,
    media_sample_GetPointer,
    media_sample_GetSize,
    media_sample_GetTime,
    media_sample_SetTime,
    media_sample_IsSyncPoint,
    media_sample_SetSyncPoint,
    media_sample_IsPreroll,
    media_sample_SetPreroll,
    media_sample_GetActualDataLength,
    media_sample_SetActualDataLength,
    media_sample_GetMediaType,
    media_sample_SetMediaType,
    media_sample_IsDiscontinuity,
    media_sample_SetDiscontinuity,
    media_sample_GetMediaTime,
    media_sample_SetMediaTime,
};

static HRESULT ddrawstreamsample_create(struct ddraw_stream *parent, IDirectDrawSurface *surface,
    const RECT *rect, IDirectDrawStreamSample **ddraw_stream_sample)
{
    struct ddraw_sample *object;
    DDSURFACEDESC desc;
    HRESULT hr;

    TRACE("(%p)\n", ddraw_stream_sample);

    if (surface)
    {
        desc.dwSize = sizeof(desc);
        if (FAILED(hr = IDirectDrawSurface_GetSurfaceDesc(surface, &desc)))
            return hr;

        if (rect)
        {
            desc.dwWidth = rect->right - rect->left;
            desc.dwHeight = rect->bottom - rect->top;
        }

        if (FAILED(hr = IDirectDrawMediaStream_SetFormat(&parent->IDirectDrawMediaStream_iface, &desc, NULL)))
            return hr;
    }

    if (!(object = calloc(1, sizeof(*object))))
        return E_OUTOFMEMORY;

    object->IDirectDrawStreamSample_iface.lpVtbl = &DirectDrawStreamSample_Vtbl;
    object->IMediaSample_iface.lpVtbl = &media_sample_vtbl;
    object->ref = 1;
    object->parent = parent;
    object->mmstream = parent->parent;
    InitializeConditionVariable(&object->update_cv);
    IAMMediaStream_AddRef(&parent->IAMMediaStream_iface);
    if (object->mmstream)
        IMultiMediaStream_AddRef(object->mmstream);
    ++parent->sample_refs;

    if (surface)
    {
        object->surface = surface;
        IDirectDrawSurface_AddRef(surface);

        if (rect)
            object->rect = *rect;
        else
            SetRect(&object->rect, 0, 0, desc.dwWidth, desc.dwHeight);
    }
    else
    {
        IDirectDraw *ddraw;

        hr = IDirectDrawMediaStream_GetDirectDraw(&parent->IDirectDrawMediaStream_iface, &ddraw);
        if (FAILED(hr))
        {
            IDirectDrawStreamSample_Release(&object->IDirectDrawStreamSample_iface);
            return hr;
        }

        desc.dwSize = sizeof(desc);
        desc.dwFlags = DDSD_CAPS|DDSD_HEIGHT|DDSD_WIDTH|DDSD_PIXELFORMAT;
        desc.dwHeight = parent->format.height;
        desc.dwWidth = parent->format.width;
        if (parent->format.flags & DDSD_PIXELFORMAT)
        {
            desc.ddpfPixelFormat = parent->format.pf;
        }
        else
        {
            desc.ddpfPixelFormat.dwSize = sizeof(desc.ddpfPixelFormat);
            desc.ddpfPixelFormat.dwFlags = DDPF_RGB;
            desc.ddpfPixelFormat.dwRGBBitCount = 32;
            desc.ddpfPixelFormat.dwRBitMask = 0xff0000;
            desc.ddpfPixelFormat.dwGBitMask = 0x00ff00;
            desc.ddpfPixelFormat.dwBBitMask = 0x0000ff;
            desc.ddpfPixelFormat.dwRGBAlphaBitMask = 0;
        }
        desc.ddsCaps.dwCaps = DDSCAPS_SYSTEMMEMORY|DDSCAPS_OFFSCREENPLAIN;
        desc.lpSurface = NULL;

        hr = IDirectDraw_CreateSurface(ddraw, &desc, &object->surface, NULL);
        IDirectDraw_Release(ddraw);
        if (FAILED(hr))
        {
            ERR("failed to create surface, 0x%08lx\n", hr);
            IDirectDrawStreamSample_Release(&object->IDirectDrawStreamSample_iface);
            return hr;
        }
    }

    *ddraw_stream_sample = &object->IDirectDrawStreamSample_iface;

    return S_OK;
}
